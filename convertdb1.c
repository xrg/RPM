#if defined(HAVE_CONFIG_H)
#include "system.h"
const char *__progname;
#else
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#endif

#include <rpmlib.h>
#include <rpmts.h>
#include <rpmdb.h>
#include <rpmio.h>
#include <rpmmacro.h>

#define FA_MAGIC      0x02050920

struct faFileHeader{
    unsigned int magic;
    unsigned int firstFree;
};

struct faHeader {
    unsigned int size;
    unsigned int freeNext; /* offset of the next free block, 0 if none */
    unsigned int freePrev;
    unsigned int isFree;

    /* note that the u16's appear last for alignment/space reasons */
};


static int fadFileSize;

static ssize_t Pread(FD_t fd, void * buf, size_t count, off_t offset) {
    if (Fseek(fd, offset, SEEK_SET) < 0)
        return -1;
    return Fread(buf, sizeof(char), count, fd);
}

static FD_t fadOpen(const char * path)
{
    struct faFileHeader newHdr;
    FD_t fd;
    struct stat stb;

    fd = Fopen(path, "r.fdio");
    if (!fd || Ferror(fd))
	return NULL;

    if (fstat(Fileno(fd), &stb)) {
	Fclose(fd);
        return NULL;
    }
    fadFileSize = stb.st_size;

    /* is this file brand new? */
    if (fadFileSize == 0) {
	Fclose(fd);
	return NULL;
    }
    if (Pread(fd, &newHdr, sizeof(newHdr), 0) != sizeof(newHdr)) {
	Fclose(fd);
	return NULL;
    }
    if (newHdr.magic != FA_MAGIC) {
	Fclose(fd);
	return NULL;
    }
    /*@-refcounttrans@*/ return fd /*@=refcounttrans@*/ ;
}

static int fadNextOffset(FD_t fd, unsigned int lastOffset)
{
    struct faHeader header;
    int offset;

    offset = (lastOffset)
	? (lastOffset - sizeof(header))
	: sizeof(struct faFileHeader);

    if (offset >= fadFileSize)
	return 0;

    if (Pread(fd, &header, sizeof(header), offset) != sizeof(header))
	return 0;

    if (!lastOffset && !header.isFree)
	return (offset + sizeof(header));

    do {
	offset += header.size;

	if (Pread(fd, &header, sizeof(header), offset) != sizeof(header))
	    return 0;

	if (!header.isFree) break;
    } while (offset < fadFileSize && header.isFree);

    if (offset < fadFileSize) {
	/* Sanity check this to make sure we're not going in loops */
	offset += sizeof(header);

	if (offset <= lastOffset) return -1;

	return offset;
    } else
	return 0;
}

static int fadFirstOffset(FD_t fd)
{
    return fadNextOffset(fd, 0);
}

/*@-boundsread@*/
static int dncmp(const void * a, const void * b)
	/*@*/
{
    const char *const * first = a;
    const char *const * second = b;
    return strcmp(*first, *second);
}
/*@=boundsread@*/

static void compressFilelist(Header h)
{
    struct rpmtd_s fileNames;
    char ** dirNames;
    const char ** baseNames;
    uint32_t * dirIndexes;
    rpm_count_t count;
    int xx, i;
    int dirIndex = -1;

    /*
     * This assumes the file list is already sorted, and begins with a
     * single '/'. That assumption isn't critical, but it makes things go
     * a bit faster.
     */

    if (headerIsEntry(h, RPMTAG_DIRNAMES)) {
	xx = headerDel(h, RPMTAG_OLDFILENAMES);
	return;		/* Already converted. */
    }

    if (!headerGet(h, RPMTAG_OLDFILENAMES, &fileNames, HEADERGET_MINMEM))
	return;
    count = rpmtdCount(&fileNames);
    if (count < 1)
	return;

    dirNames = xmalloc(sizeof(*dirNames) * count);	/* worst case */
    baseNames = xmalloc(sizeof(*dirNames) * count);
    dirIndexes = xmalloc(sizeof(*dirIndexes) * count);

    /* HACK. Source RPM, so just do things differently */
    {	const char *fn = rpmtdGetString(&fileNames);
	if (fn && *fn != '/') {
	    dirIndex = 0;
	    dirNames[dirIndex] = xstrdup("");
	    while ((i = rpmtdNext(&fileNames)) >= 0) {
		dirIndexes[i] = dirIndex;
		baseNames[i] = rpmtdGetString(&fileNames);
	    }
	    goto exit;
	}
    }

    while ((i = rpmtdNext(&fileNames)) >= 0) {
	char ** needle;
	char savechar;
	char * baseName;
	size_t len;
	const char *filename = rpmtdGetString(&fileNames);

	if (filename == NULL)	/* XXX can't happen */
	    continue;
	baseName = strrchr(filename, '/') + 1;
	len = baseName - filename;
	needle = dirNames;
	savechar = *baseName;
	*baseName = '\0';
	if (dirIndex < 0 ||
	    (needle = bsearch(&filename, dirNames, dirIndex + 1, sizeof(dirNames[0]), dncmp)) == NULL) {
	    char *s = xmalloc(len + 1);
	    rstrlcpy(s, filename, len + 1);
	    dirIndexes[i] = ++dirIndex;
	    dirNames[dirIndex] = s;
	} else
	    dirIndexes[i] = needle - dirNames;

	*baseName = savechar;
	baseNames[i] = baseName;
    }

exit:
    if (count > 0) {
	headerPutUint32(h, RPMTAG_DIRINDEXES, dirIndexes, count);
	headerPutStringArray(h, RPMTAG_BASENAMES, baseNames, count);
	headerPutStringArray(h, RPMTAG_DIRNAMES,
			     (const char **) dirNames, dirIndex + 1);
    }

    rpmtdFreeData(&fileNames);
    for (i = 0; i <= dirIndex; i++) {
	free(dirNames[i]);
    }
    free(dirNames);
    free(baseNames);
    free(dirIndexes);

    xx = headerDel(h, RPMTAG_OLDFILENAMES);
}

/*
 * Up to rpm 3.0.4, packages implicitly provided their own name-version-release.
 * Retrofit an explicit "Provides: name = epoch:version-release.
 */
static void providePackageNVR(Header h)
{
    const char *name;
    char *pEVR;
    rpmsenseFlags pFlags = RPMSENSE_EQUAL;
    int bingo = 1;
    struct rpmtd_s pnames;
    rpmds hds, nvrds;

    /* Generate provides for this package name-version-release. */
    pEVR = headerGetEVR(h, &name);
    if (!(name && pEVR))
	return;

    /*
     * Rpm prior to 3.0.3 does not have versioned provides.
     * If no provides at all are available, we can just add.
     */
    if (!headerGet(h, RPMTAG_PROVIDENAME, &pnames, HEADERGET_MINMEM)) {
	goto exit;
    }

    /*
     * Otherwise, fill in entries on legacy packages.
     */
    if (!headerIsEntry(h, RPMTAG_PROVIDEVERSION)) {
	while (rpmtdNext(&pnames) >= 0) {
	    rpmsenseFlags fdummy = RPMSENSE_ANY;

	    headerPutString(h, RPMTAG_PROVIDEVERSION, "");
	    headerPutUint32(h, RPMTAG_PROVIDEFLAGS, &fdummy, 1);
	}
	goto exit;
    }

    /* see if we already have this provide */
    hds = rpmdsNew(h, RPMTAG_PROVIDENAME, 0);
    nvrds = rpmdsSingle(RPMTAG_PROVIDENAME, name, pEVR, pFlags);
    if (rpmdsFind(hds, nvrds) >= 0) {
	bingo = 0;
    }
    rpmdsFree(hds);
    rpmdsFree(nvrds);

exit:
    if (bingo) {
	const char *evr = pEVR;
	headerPutString(h, RPMTAG_PROVIDENAME, name);
	headerPutString(h, RPMTAG_PROVIDEVERSION, evr);
	headerPutUint32(h, RPMTAG_PROVIDEFLAGS, &pFlags, 1);
    }
    rpmtdFreeData(&pnames);
    free(pEVR);
}
/*@=bounds@*/

static rpmdb db;

int
main(int argc, char ** argv)
{
  FD_t fd;
  int offset;
  Header h;
  const char *name;
  const char *version;
  const char *release;
  rpmts ts;

  if (argc != 2)
    {
      fprintf(stderr, "usage: %s <packages.rpm>\n", argv[0]);
      exit(1);
    }
  if ((fd = fadOpen(argv[1])) == 0)
    {
      fprintf(stderr, "could not open %s\n", argv[1]);
      exit(1);
    }
  rpmInitMacros(NULL, "/usr/lib/rpm/macros");

  /* speed things up */
  (void) rpmDefineMacro(NULL, "_rpmdb_rebuild %{nil}", -1);

  ts = rpmtsCreate();

  if (rpmtsOpenDB(ts, O_RDWR)) {
    fprintf(stderr, "could not open rpm database\n");
    exit(1);
  }

  for (offset = fadFirstOffset(fd); offset; offset = fadNextOffset(fd, offset))
    {
      rpmdbMatchIterator mi;

      /* have to use lseek instead of Fseek because headerRead
       * uses low level IO
       */
      if (lseek(Fileno(fd), (off_t)offset, SEEK_SET) == -1)
	{
	  perror("lseek");
	  continue;
	}
      h = headerRead(fd, HEADER_MAGIC_NO);
      if (!h)
	continue;
      compressFilelist(h);
      providePackageNVR(h);
      headerNVR(h, &name, &version, &release);
      mi = rpmdbInitIterator(db, RPMTAG_NAME, name, 0);
      rpmdbSetIteratorRE(mi, RPMTAG_VERSION, RPMMIRE_DEFAULT, version);
      rpmdbSetIteratorRE(mi, RPMTAG_RELEASE, RPMMIRE_DEFAULT, release);
      if (rpmdbNextIterator(mi))
        {
	  printf("%s-%s-%s is already in database\n", name, version, release);
	  rpmdbFreeIterator(mi);
	  headerFree(h);
	  continue;
        }
      rpmdbFreeIterator(mi);
      if (rpmtsHeaderAddDB(ts, h))
	{
	  fprintf(stderr, "could not add %s-%s-%s!\n", name, version, release);
	}
      headerFree(h);
    }
  Fclose(fd);
  rpmtsCloseDB(ts);
  rpmtsFree(ts);
  return 0;
}

