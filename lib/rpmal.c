/** \ingroup rpmdep
 * \file lib/rpmal.c
 */

#include "system.h"


#include <rpm/rpmal.h>
#include <rpm/rpmds.h>
#include <rpm/rpmfi.h>

#include "debug.h"

typedef struct availablePackage_s * availablePackage;

int _rpmal_debug = 0;


/** \ingroup rpmdep
 * Info about a single package to be installed.
 */
struct availablePackage_s {
    rpmds provides;		/*!< Provides: dependencies. */
    rpmfi fi;			/*!< File info set. */

    rpm_color_t tscolor;	/*!< Transaction color bits. */

    fnpyKey key;		/*!< Associated file name/python object */

};

typedef struct availableIndexEntry_s *	availableIndexEntry;

/** \ingroup rpmdep
 * A single available item (e.g. a Provides: dependency).
 */
struct availableIndexEntry_s {
    rpmalKey pkgKey;		/*!< Containing package. */
    const char * entry;		/*!< Dependency name. */
    unsigned short entryLen;	/*!< No. of bytes in name. */
    unsigned short entryIx;	/*!< Dependency index. */
    enum indexEntryType {
	IET_PROVIDES=1			/*!< A Provides: dependency. */
    } type;			/*!< Type of available item. */
};

typedef struct availableIndex_s *	availableIndex;

/** \ingroup rpmdep
 * Index of all available items.
 */
struct availableIndex_s {
    availableIndexEntry index;	/*!< Array of available items. */
    int size;			/*!< No. of available items. */
    int k;			/*!< Current index. */
};

typedef struct fileIndexEntry_s *	fileIndexEntry;

/** \ingroup rpmdep
 * A file to be installed/removed.
 */
struct fileIndexEntry_s {
    const char * baseName;	/*!< File basename. */
    int baseNameLen;
    rpmalNum pkgNum;		/*!< Containing package index. */
    rpm_color_t ficolor;
};

typedef struct dirInfo_s *		dirInfo;

/** \ingroup rpmdep
 * A directory to be installed/removed.
 */
struct dirInfo_s {
    char * dirName;		/*!< Directory path (+ trailing '/'). */
    int dirNameLen;		/*!< No. bytes in directory path. */
    fileIndexEntry files;	/*!< Array of files in directory. */
    int numFiles;		/*!< No. files in directory. */
};

/** \ingroup rpmdep
 * Set of available packages, items, and directories.
 */
struct rpmal_s {
    availablePackage list;	/*!< Set of packages. */
    struct availableIndex_s index;	/*!< Set of available items. */
    int delta;			/*!< Delta for pkg list reallocation. */
    int size;			/*!< No. of pkgs in list. */
    int alloced;		/*!< No. of pkgs allocated for list. */
    rpm_color_t tscolor;	/*!< Transaction color. */
    int numDirs;		/*!< No. of directories. */
    dirInfo dirs;		/*!< Set of directories. */
};

/**
 * Destroy available item index.
 * @param al		available list
 */
static void rpmalFreeIndex(rpmal al)
{
    availableIndex ai = &al->index;
    if (ai->size > 0) {
	ai->index = _free(ai->index);
	ai->size = 0;
    }
}

static inline rpmalNum alKey2Num(const rpmal al,
		rpmalKey pkgKey)
{
    return ((rpmalNum)pkgKey);
}

static inline rpmalKey alNum2Key(const rpmal al,
		rpmalNum pkgNum)
{
    return ((rpmalKey)pkgNum);
}

rpmal rpmalCreate(int delta)
{
    rpmal al = xcalloc(1, sizeof(*al));
    availableIndex ai = &al->index;

    al->delta = delta;
    al->size = 0;
    al->list = xcalloc(al->delta, sizeof(*al->list));
    al->alloced = al->delta;

    ai->index = NULL;
    ai->size = 0;

    al->numDirs = 0;
    al->dirs = NULL;
    return al;
}

rpmal rpmalFree(rpmal al)
{
    availablePackage alp;
    dirInfo die;
    int i;

    if (al == NULL)
	return NULL;

    if ((alp = al->list) != NULL)
    for (i = 0; i < al->size; i++, alp++) {
	alp->provides = rpmdsFree(alp->provides);
	alp->fi = rpmfiFree(alp->fi);
    }

    if ((die = al->dirs) != NULL)
    for (i = 0; i < al->numDirs; i++, die++) {
	die->dirName = _free(die->dirName);
	die->files = _free(die->files);
    }
    al->dirs = _free(al->dirs);
    al->numDirs = 0;

    al->list = _free(al->list);
    al->alloced = 0;
    rpmalFreeIndex(al);
    al = _free(al);
    return NULL;
}

/**
 * Compare two directory info entries by name (qsort/bsearch).
 * @param one		1st directory info
 * @param two		2nd directory info
 * @return		result of comparison
 */
static int dieCompare(const void * one, const void * two)
{
    const dirInfo a = (const dirInfo) one;
    const dirInfo b = (const dirInfo) two;
    int lenchk = a->dirNameLen - b->dirNameLen;

    if (lenchk || a->dirNameLen == 0)
	return lenchk;

    if (a->dirName == NULL || b->dirName == NULL)
	return lenchk;

    /* XXX FIXME: this might do "backward" strcmp for speed */
    return strcmp(a->dirName, b->dirName);
}

/**
 * Compare two file info entries by name (qsort/bsearch).
 * @param one		1st directory info
 * @param two		2nd directory info
 * @return		result of comparison
 */
static int fieCompare(const void * one, const void * two)
{
    const fileIndexEntry a = (const fileIndexEntry) one;
    const fileIndexEntry b = (const fileIndexEntry) two;
    int lenchk = a->baseNameLen - b->baseNameLen;

    if (lenchk)
	return lenchk;

    if (a->baseName == NULL || b->baseName == NULL)
	return lenchk;

#ifdef	NOISY
if (_rpmal_debug) {
fprintf(stderr, "\t\tstrcmp(%p:%p, %p:%p)", a, a->baseName, b, b->baseName);
#if 0
fprintf(stderr, " a %s", a->baseName);
#endif
fprintf(stderr, " b %s", a->baseName);
fprintf(stderr, "\n");
}
#endif

    return strcmp(a->baseName, b->baseName);
}

void rpmalDel(rpmal al, rpmalKey pkgKey)
{
    rpmalNum pkgNum = alKey2Num(al, pkgKey);
    availablePackage alp;
    rpmfi fi;

    if (al == NULL || al->list == NULL)
	return;		/* XXX can't happen */

    alp = al->list + pkgNum;

if (_rpmal_debug)
fprintf(stderr, "*** del %p[%d]\n", al->list, (int) pkgNum);

    /* Delete directory/file info entries from added package list. */
    if ((fi = alp->fi) != NULL)
    if (rpmfiFC(fi) > 0) {
	int origNumDirs = al->numDirs;
	int dx;
	struct dirInfo_s dieNeedle;
	dirInfo die;
	int last;
	int i;

	memset(&dieNeedle, 0, sizeof(dieNeedle));
	/* XXX FIXME: We ought to relocate the directory list here */

	if (al->dirs != NULL)
	for (dx = rpmfiDC(fi) - 1; dx >= 0; dx--)
	{
	    fileIndexEntry fie;

	    (void) rpmfiSetDX(fi, dx);

	    /* XXX: reference to within rpmfi, must not be freed  */
	    dieNeedle.dirName = (char *) rpmfiDN(fi);
	    dieNeedle.dirNameLen = (dieNeedle.dirName != NULL
			? strlen(dieNeedle.dirName) : 0);
	    die = bsearch(&dieNeedle, al->dirs, al->numDirs,
			       sizeof(dieNeedle), dieCompare);
	    if (die == NULL)
		continue;

if (_rpmal_debug)
fprintf(stderr, "--- die[%5ld] %p [%3d] %s\n", (long)(die - al->dirs), die, die->dirNameLen, die->dirName);

	    last = die->numFiles;
	    fie = die->files + last - 1;
	    for (i = last - 1; i >= 0; i--, fie--) {
		if (fie->pkgNum != pkgNum)
		    continue;
		die->numFiles--;

		if (i < die->numFiles) {
if (_rpmal_debug)
fprintf(stderr, "\t%p[%3d] memmove(%p:%p,%p:%p,0x%lx) %s <- %s\n", die->files, die->numFiles, fie, fie->baseName, fie+1, (fie+1)->baseName, (long)((die->numFiles - i) * sizeof(*fie)), fie->baseName, (fie+1)->baseName);

		    memmove(fie, fie+1, (die->numFiles - i) * sizeof(*fie));
		}
if (_rpmal_debug)
fprintf(stderr, "\t%p[%3d] memset(%p,0,0x%lx) %p [%3d] %s\n", die->files, die->numFiles, die->files + die->numFiles, (long) sizeof(*fie), fie->baseName, fie->baseNameLen, fie->baseName);
		memset(die->files + die->numFiles, 0, sizeof(*fie)); /* overkill */

	    }
	    if (die->numFiles > 0) {
		if (last > i)
		    die->files = xrealloc(die->files,
					die->numFiles * sizeof(*die->files));
		continue;
	    }
	    die->files = _free(die->files);
	    die->dirName = _free(die->dirName);
	    al->numDirs--;
	    if ((die - al->dirs) < al->numDirs) {
if (_rpmal_debug)
fprintf(stderr, "    die[%5ld] memmove(%p,%p,0x%lx)\n", (long) (die - al->dirs), die, die+1, (long) ((al->numDirs - (die - al->dirs)) * sizeof(*die)));

		memmove(die, die+1, (al->numDirs - (die - al->dirs)) * sizeof(*die));
	    }

if (_rpmal_debug)
fprintf(stderr, "    die[%5d] memset(%p,0,0x%lx)\n", al->numDirs, al->dirs + al->numDirs, (long) sizeof(*die));
	    memset(al->dirs + al->numDirs, 0, sizeof(*al->dirs)); /* overkill */
	}

	if (origNumDirs > al->numDirs) {
	    if (al->numDirs > 0)
		al->dirs = xrealloc(al->dirs, al->numDirs * sizeof(*al->dirs));
	    else
		al->dirs = _free(al->dirs);
	}
    }

    alp->provides = rpmdsFree(alp->provides);
    alp->fi = rpmfiFree(alp->fi);

    memset(alp, 0, sizeof(*alp));	/* XXX trash and burn */
    return;
}

rpmalKey rpmalAdd(rpmal * alistp, rpmalKey pkgKey, fnpyKey key,
		rpmds provides, rpmfi fi, rpm_color_t tscolor)
{
    rpmalNum pkgNum;
    rpmal al;
    availablePackage alp;

    /* If list doesn't exist yet, create. */
    if (*alistp == NULL)
	*alistp = rpmalCreate(5);
    al = *alistp;
    pkgNum = alKey2Num(al, pkgKey);

    if (pkgNum >= 0 && pkgNum < al->size) {
	rpmalDel(al, pkgKey);
    } else {
	if (al->size == al->alloced) {
	    al->alloced += al->delta;
	    al->list = xrealloc(al->list, sizeof(*al->list) * al->alloced);
	}
	pkgNum = al->size++;
    }

    if (al->list == NULL)
	return RPMAL_NOMATCH;		/* XXX can't happen */

    alp = al->list + pkgNum;

    alp->key = key;
    alp->tscolor = tscolor;

if (_rpmal_debug)
fprintf(stderr, "*** add %p[%d] 0x%x\n", al->list, (int) pkgNum, tscolor);

    alp->provides = rpmdsLink(provides, RPMDBG_M("Provides (rpmalAdd)"));
    alp->fi = rpmfiLink(fi, RPMDBG_M("Files (rpmalAdd)"));

    fi = rpmfiLink(alp->fi, RPMDBG_M("Files index (rpmalAdd)"));
    fi = rpmfiInit(fi, 0);
    if (rpmfiFC(fi) > 0) {
	struct dirInfo_s dieNeedle; 
	dirInfo die;
	int dc = rpmfiDC(fi);
	int dx;
	int *dirMapping = xmalloc(sizeof(*dirMapping) * dc);
	int *dirUnique = xmalloc(sizeof(*dirUnique) * dc);
	const char * DN;
	int origNumDirs;
	int first;
	int i = 0;

	memset(&dieNeedle, 0, sizeof(dieNeedle));
	/* XXX FIXME: We ought to relocate the directory list here */

	/* XXX enough space for all directories, late realloc to truncate. */
	al->dirs = xrealloc(al->dirs, (al->numDirs + dc) * sizeof(*al->dirs));

	/* Only previously allocated dirInfo is sorted and bsearch'able. */
	origNumDirs = al->numDirs;

	/* Package dirnames are not currently unique. Create unique mapping. */
	for (dx = 0; dx < dc; dx++) {
	    (void) rpmfiSetDX(fi, dx);
	    DN = rpmfiDN(fi);
	    if (DN != NULL)
	    for (i = 0; i < dx; i++) {
		const char * iDN;
		(void) rpmfiSetDX(fi, i);
		iDN = rpmfiDN(fi);
		if (iDN != NULL && !strcmp(DN, iDN))
		    break;
	    }
	    dirUnique[dx] = i;
	}

	/* Map package dirs into transaction dirInfo index. */
	for (dx = 0; dx < dc; dx++) {

	    /* Non-unique package dirs use the 1st entry mapping. */
	    if (dirUnique[dx] < dx) {
		dirMapping[dx] = dirMapping[dirUnique[dx]];
		continue;
	    }

	    /* Find global dirInfo mapping for first encounter. */
	    (void) rpmfiSetDX(fi, dx);

	    {   DN = rpmfiDN(fi);

	        /* XXX: reference to within rpmfi, must not be freed */
		dieNeedle.dirName = (char *) DN;
	    }

	    dieNeedle.dirNameLen = (dieNeedle.dirName != NULL
			? strlen(dieNeedle.dirName) : 0);
	    die = bsearch(&dieNeedle, al->dirs, origNumDirs,
			       sizeof(dieNeedle), dieCompare);
	    if (die) {
		dirMapping[dx] = die - al->dirs;
	    } else {
		dirMapping[dx] = al->numDirs;
		die = al->dirs + al->numDirs;
		if (dieNeedle.dirName != NULL)
		    die->dirName = xstrdup(dieNeedle.dirName);
		die->dirNameLen = dieNeedle.dirNameLen;
		die->files = NULL;
		die->numFiles = 0;
if (_rpmal_debug)
fprintf(stderr, "+++ die[%5d] %p [%3d] %s\n", al->numDirs, die, die->dirNameLen, die->dirName);

		al->numDirs++;
	    }
	}

	for (first = rpmfiNext(fi); first >= 0;) {
	    fileIndexEntry fie;
	    int next;

	    /* Find the first file of the next directory. */
	    dx = rpmfiDX(fi);
	    while ((next = rpmfiNext(fi)) >= 0) {
		if (dx != rpmfiDX(fi))
		    break;
	    }
	    if (next < 0) next = rpmfiFC(fi);	/* XXX reset end-of-list */

	    die = al->dirs + dirMapping[dx];
	    die->files = xrealloc(die->files,
			(die->numFiles + next - first) * sizeof(*die->files));

	    fie = die->files + die->numFiles;

if (_rpmal_debug)
fprintf(stderr, "    die[%5d] %p->files [%p[%d],%p) -> [%p[%d],%p)\n", dirMapping[dx], die,
die->files, die->numFiles, die->files+die->numFiles,
fie, (next - first), fie + (next - first));

	    /* Rewind to first file, generate file index entry for each file. */
	    fi = rpmfiInit(fi, first);
	    while ((first = rpmfiNext(fi)) >= 0 && first < next) {
		fie->baseName = rpmfiBN(fi);
		fie->baseNameLen = (fie->baseName ? strlen(fie->baseName) : 0);
		fie->pkgNum = pkgNum;
		fie->ficolor = rpmfiFColor(fi);
if (_rpmal_debug)
fprintf(stderr, "\t%p[%3d] %p:%p[%2d] %s\n", die->files, die->numFiles, fie, fie->baseName, fie->baseNameLen, rpmfiFN(fi));

		die->numFiles++;
		fie++;
	    }
	    qsort(die->files, die->numFiles, sizeof(*die->files), fieCompare);
	}

	/* Resize the directory list. If any directories were added, resort. */
	al->dirs = xrealloc(al->dirs, al->numDirs * sizeof(*al->dirs));
	if (origNumDirs != al->numDirs)
	    qsort(al->dirs, al->numDirs, sizeof(*al->dirs), dieCompare);
	free(dirUnique);
	free(dirMapping);
    }
    fi = rpmfiUnlink(fi, RPMDBG_M("Files index (rpmalAdd)"));

    rpmalFreeIndex(al);

assert(((rpmalNum)(alp - al->list)) == pkgNum);
    return ((rpmalKey)(alp - al->list));
}

/**
 * Compare two available index entries by name (qsort/bsearch).
 * @param one		1st available index entry
 * @param two		2nd available index entry
 * @return		result of comparison
 */
static int indexcmp(const void * one, const void * two)
{
    const availableIndexEntry a = (const availableIndexEntry) one;
    const availableIndexEntry b = (const availableIndexEntry) two;
    int lenchk;

    lenchk = a->entryLen - b->entryLen;
    if (lenchk)
	return lenchk;

    return strcmp(a->entry, b->entry);
}

void rpmalAddProvides(rpmal al, rpmalKey pkgKey, rpmds provides, rpm_color_t tscolor)
{
    rpm_color_t dscolor;
    const char * Name;
    rpmalNum pkgNum = alKey2Num(al, pkgKey);
    availableIndex ai = &al->index;
    availableIndexEntry aie;
    int ix;

    if (provides == NULL || pkgNum < 0 || pkgNum >= al->size)
	return;
    if (ai->index == NULL || ai->k < 0 || ai->k >= ai->size)
	return;

    if (rpmdsInit(provides) != NULL)
    while (rpmdsNext(provides) >= 0) {

	if ((Name = rpmdsN(provides)) == NULL)
	    continue;	/* XXX can't happen */

	/* Ignore colored provides not in our rainbow. */
	dscolor = rpmdsColor(provides);
	if (tscolor && dscolor && !(tscolor & dscolor))
	    continue;

	aie = ai->index + ai->k;
	ai->k++;

	aie->pkgKey = pkgKey;
	aie->entry = Name;
	aie->entryLen = strlen(Name);
	ix = rpmdsIx(provides);

/* XXX make sure that element index fits in unsigned short */
assert(ix < 0x10000);

	aie->entryIx = ix;
	aie->type = IET_PROVIDES;
    }
}

void rpmalMakeIndex(rpmal al)
{
    availableIndex ai;
    availablePackage alp;
    intptr_t i;

    if (al == NULL || al->list == NULL) return;
    ai = &al->index;

    ai->size = 0;
    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	if (alp->provides != NULL)
	    ai->size += rpmdsCount(alp->provides);
    }
    if (ai->size == 0) return;

    ai->index = xrealloc(ai->index, ai->size * sizeof(*ai->index));
    ai->k = 0;
    for (i = 0; i < al->size; i++) {
	alp = al->list + i;
	rpmalAddProvides(al, (rpmalKey)i, alp->provides, alp->tscolor);
    }

    /* Reset size to the no. of provides added. */
    ai->size = ai->k;
    qsort(ai->index, ai->size, sizeof(*ai->index), indexcmp);
}

fnpyKey *
rpmalAllFileSatisfiesDepend(const rpmal al, const rpmds ds, rpmalKey * keyp)
{
    rpm_color_t tscolor;
    rpm_color_t ficolor;
    int found = 0;
    char * dirName;
    const char * baseName;
    struct dirInfo_s dieNeedle; 
    dirInfo die;
    struct fileIndexEntry_s fieNeedle;
    fileIndexEntry fie;
    availablePackage alp;
    fnpyKey * ret = NULL;
    const char * fileName;

    memset(&dieNeedle, 0, sizeof(dieNeedle));
    memset(&fieNeedle, 0, sizeof(fieNeedle));

    if (keyp) *keyp = RPMAL_NOMATCH;

    if (al == NULL || (fileName = rpmdsN(ds)) == NULL || *fileName != '/')
	return NULL;

    /* Solaris 2.6 bsearch sucks down on this. */
    if (al->numDirs == 0 || al->dirs == NULL || al->list == NULL)
	return NULL;

    {	char * t;
	dirName = t = xstrdup(fileName);
	if ((t = strrchr(t, '/')) != NULL) {
	    t++;		/* leave the trailing '/' */
	    *t = '\0';
	}
    }

    dieNeedle.dirName = (char *) dirName;
    dieNeedle.dirNameLen = strlen(dirName);
    die = bsearch(&dieNeedle, al->dirs, al->numDirs,
		       sizeof(dieNeedle), dieCompare);
    if (die == NULL)
	goto exit;

    /* rewind to the first match */
    while (die > al->dirs && dieCompare(die-1, &dieNeedle) == 0)
	die--;

    if ((baseName = strrchr(fileName, '/')) == NULL)
	goto exit;
    baseName++;

    /* FIX: ret is a problem */
    for (found = 0, ret = NULL;
	 die < al->dirs + al->numDirs && dieCompare(die, &dieNeedle) == 0;
	 die++)
    {

if (_rpmal_debug)
fprintf(stderr, "==> die %p %s\n", die, (die->dirName ? die->dirName : "(nil)"));

	fieNeedle.baseName = baseName;
	fieNeedle.baseNameLen = strlen(fieNeedle.baseName);
	fie = bsearch(&fieNeedle, die->files, die->numFiles,
		       sizeof(fieNeedle), fieCompare);
	if (fie == NULL)
	    continue;	/* XXX shouldn't happen */

if (_rpmal_debug)
fprintf(stderr, "==> fie %p %s\n", fie, (fie->baseName ? fie->baseName : "(nil)"));

	alp = al->list + fie->pkgNum;

        /* Ignore colored files not in our rainbow. */
	tscolor = alp->tscolor;
	ficolor = fie->ficolor;
        if (tscolor && ficolor && !(tscolor & ficolor))
            continue;

	rpmdsNotify(ds, _("(added files)"), 0);

	ret = xrealloc(ret, (found+2) * sizeof(*ret));
	if (ret)	/* can't happen */
	    ret[found] = alp->key;
	if (keyp)
	    *keyp = alNum2Key(al, fie->pkgNum);
	found++;
    }

exit:
    dirName = _free(dirName);
    if (ret)
	ret[found] = NULL;
    return ret;
}

fnpyKey *
rpmalAllSatisfiesDepend(const rpmal al, const rpmds ds, rpmalKey * keyp)
{
    availableIndex ai;
    struct availableIndexEntry_s needle;
    availableIndexEntry match;
    fnpyKey * ret = NULL;
    int found = 0;
    const char * KName;
    availablePackage alp;
    int rc;

    if (keyp) *keyp = RPMAL_NOMATCH;

    if (al == NULL || ds == NULL || (KName = rpmdsN(ds)) == NULL)
	return ret;

    if (*KName == '/') {
	/* First, look for files "contained" in package ... */
	ret = rpmalAllFileSatisfiesDepend(al, ds, keyp);
	if (ret != NULL && *ret != NULL)
	    return ret;
	/* ... then, look for files "provided" by package. */
	ret = _free(ret);
    }

    ai = &al->index;
    if (ai->index == NULL || ai->size <= 0)
	return NULL;

    memset(&needle, 0, sizeof(needle));
    needle.entry = KName;
    needle.entryLen = strlen(needle.entry);

    match = bsearch(&needle, ai->index, ai->size, sizeof(*ai->index), indexcmp);
    if (match == NULL)
	return NULL;

    /* rewind to the first match */
    while (match > ai->index && indexcmp(match-1, &needle) == 0)
	match--;

    if (al->list != NULL)	/* XXX always true */
    for (ret = NULL, found = 0;
	 match < ai->index + ai->size && indexcmp(match, &needle) == 0;
	 match++)
    {
	alp = al->list + alKey2Num(al, match->pkgKey);

	rc = 0;
	if (alp->provides != NULL)	/* XXX can't happen */
	switch (match->type) {
	case IET_PROVIDES:
	    /* XXX single step on rpmdsNext to regenerate DNEVR string */
	    (void) rpmdsSetIx(alp->provides, match->entryIx - 1);
	    if (rpmdsNext(alp->provides) >= 0)
		rc = rpmdsCompare(alp->provides, ds);

	    if (rc)
		rpmdsNotify(ds, _("(added provide)"), 0);

	    break;
	}

	if (rc) {
	    ret = xrealloc(ret, (found + 2) * sizeof(*ret));
	    if (ret)	/* can't happen */
		ret[found] = alp->key;
	    if (keyp)
		*keyp = match->pkgKey;
	    found++;
	}
    }

    if (ret)
	ret[found] = NULL;

/* FIX: *keyp may be NULL */
    return ret;
}

fnpyKey
rpmalSatisfiesDepend(const rpmal al, const rpmds ds, rpmalKey * keyp)
{
    fnpyKey * tmp = rpmalAllSatisfiesDepend(al, ds, keyp);

    if (tmp) {
	fnpyKey ret = tmp[0];
	free(tmp);
	return ret;
    }
    return NULL;
}
