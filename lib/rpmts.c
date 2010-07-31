/** \ingroup rpmdep
 * \file lib/rpmts.c
 * Routine(s) to handle a "rpmts" transaction sets.
 */
#include "system.h"

#include <inttypes.h>

#include <rpm/rpmtypes.h>
#include <rpm/rpmlib.h>			/* rpmReadPackage etc */
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmfileutil.h>		/* rpmtsOpenDB() needs rpmGetPath */
#include <rpm/rpmstring.h>
#include <rpm/rpmkeyring.h>

#include <rpm/rpmdb.h>
#include <rpm/rpmal.h>
#include <rpm/rpmds.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmte.h>

#include "rpmio/digest.h"
#include "lib/rpmlock.h"
#include "lib/rpmts_internal.h"

/* XXX FIXME: merge with existing (broken?) tests in system.h */
/* portability fiddles */
#if STATFS_IN_SYS_STATVFS
#include <sys/statvfs.h>

#else
# if STATFS_IN_SYS_VFS
#  include <sys/vfs.h>
# else
#  if STATFS_IN_SYS_MOUNT
#   include <sys/mount.h>
#  else
#   if STATFS_IN_SYS_STATFS
#    include <sys/statfs.h>
#   endif
#  endif
# endif
#endif

#include "debug.h"

static void loadKeyring(rpmts ts);

int _rpmts_debug = 0;

int _rpmts_stats = 0;

rpmts rpmtsUnlink(rpmts ts, const char * msg)
{
if (_rpmts_debug)
fprintf(stderr, "--> ts %p -- %d %s\n", ts, ts->nrefs, msg);
    ts->nrefs--;
    return NULL;
}

rpmts rpmtsLink(rpmts ts, const char * msg)
{
    ts->nrefs++;
if (_rpmts_debug)
fprintf(stderr, "--> ts %p ++ %d %s\n", ts, ts->nrefs, msg);
    return ts;
}

int rpmtsCloseDB(rpmts ts)
{
    int rc = 0;

    if (ts->rdb != NULL) {
	(void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_DBGET), 
			rpmdbOp(ts->rdb, RPMDB_OP_DBGET));
	(void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_DBPUT),
			rpmdbOp(ts->rdb, RPMDB_OP_DBPUT));
	(void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_DBDEL),
			rpmdbOp(ts->rdb, RPMDB_OP_DBDEL));
	rc = rpmdbClose(ts->rdb);
	ts->rdb = NULL;
    }
    return rc;
}

int rpmtsOpenDB(rpmts ts, int dbmode)
{
    int rc = 0;

    if (ts->rdb != NULL && ts->dbmode == dbmode)
	return 0;

    (void) rpmtsCloseDB(ts);

    /* XXX there's a potential db lock race here. */

    ts->dbmode = dbmode;
    rc = rpmdbOpen(ts->rootDir, &ts->rdb, ts->dbmode, 0644);
    if (rc) {
	char * dn = rpmGetPath(ts->rootDir, "%{_dbpath}", NULL);
	rpmlog(RPMLOG_ERR,
			_("cannot open Packages database in %s\n"), dn);
	dn = _free(dn);
    }
    return rc;
}

int rpmtsInitDB(rpmts ts, int dbmode)
{
    void *lock = rpmtsAcquireLock(ts);
    int rc = -1;
    if (lock)
	    rc = rpmdbInit(ts->rootDir, dbmode);
    rpmtsFreeLock(lock);
    return rc;
}

int rpmtsGetDBMode(rpmts ts)
{
    assert(ts != NULL);
    return (ts->dbmode);
}

int rpmtsSetDBMode(rpmts ts, int dbmode)
{
    int rc = 1;
    /* mode setting only permitted on non-open db */
    if (ts != NULL && rpmtsGetRdb(ts) == NULL) {
    	ts->dbmode = dbmode;
	rc = 0;
    }
    return rc;
}


int rpmtsRebuildDB(rpmts ts)
{
    int rc;
    void *lock = rpmtsAcquireLock(ts);
    if (!lock) return -1;
    if (!(ts->vsflags & RPMVSF_NOHDRCHK))
	rc = rpmdbRebuild(ts->rootDir, ts, headerCheck);
    else
	rc = rpmdbRebuild(ts->rootDir, NULL, NULL);
    rpmtsFreeLock(lock);
    return rc;
}

int rpmtsVerifyDB(rpmts ts)
{
    return rpmdbVerify(ts->rootDir);
}

static int isArch(const char * arch)
{
    const char * const * av;
    static const char * const arches[] = {
	"i386", "i486", "i586", "i686", "athlon", "pentium3", "pentium4", "x86_64", "amd64", "ia32e", "geode",
	"alpha", "alphaev5", "alphaev56", "alphapca56", "alphaev6", "alphaev67",
	"sparc", "sun4", "sun4m", "sun4c", "sun4d", "sparcv8", "sparcv9", "sparcv9v",
	"sparc64", "sparc64v", "sun4u",
	"mips", "mipsel", "IP",
	"ppc", "ppciseries", "ppcpseries",
	"ppc64", "ppc64iseries", "ppc64pseries",
	"m68k",
	"rs6000",
	"ia64",
	"armv3l", "armv4b", "armv4l", "armv4tl", "armv5tl", "armv5tel", "armv5tejl", "armv6l",
	"s390", "i370", "s390x",
	"sh", "xtensa",
	"noarch",
	NULL,
    };

    for (av = arches; *av != NULL; av++) {
	if (!strcmp(arch, *av))
	    return 1;
    }
    return 0;
}

/* keyp might no be defined. */
rpmdbMatchIterator rpmtsInitIterator(const rpmts ts, rpmTag rpmtag,
			const void * keyp, size_t keylen)
{
    rpmdbMatchIterator mi = NULL;
    const char * arch = NULL;
    char *tmp = NULL;
    int xx;

    if (ts->keyring == NULL)
	loadKeyring(ts);

    if (ts->rdb == NULL && rpmtsOpenDB(ts, ts->dbmode))
	return NULL;

    /* Parse out "N(EVR).A" tokens from a label key. */
    if (rpmtag == RPMDBI_LABEL && keyp != NULL) {
	const char *se, *s = keyp;
	char *t;
	size_t slen = strlen(s);
	int level = 0;
	int c;

	tmp = xmalloc(slen+1);
	keyp = t = tmp;
	while ((c = *s++) != '\0') {
	    switch (c) {
	    default:
		*t++ = c;
		break;
	    case '(':
		/* XXX Fail if nested parens. */
		if (level++ != 0) {
		    rpmlog(RPMLOG_ERR, _("extra '(' in package label: %s\n"), (const char*)keyp);
		    goto exit;
		}
		/* Parse explicit epoch. */
		for (se = s; *se && risdigit(*se); se++)
		    {};
		if (*se == ':') {
		    /* XXX skip explicit epoch's (for now) */
		    *t++ = '-';
		    s = se + 1;
		} else {
		    /* No Epoch: found. Convert '(' to '-' and chug. */
		    *t++ = '-';
		}
		break;
	    case ')':
		/* XXX Fail if nested parens. */
		if (--level != 0) {
		    rpmlog(RPMLOG_ERR, _("missing '(' in package label: %s\n"), (const char*)keyp);
		    goto exit;
		}
		/* Don't copy trailing ')' */
		break;
	    }
	}
	if (level) {
	    rpmlog(RPMLOG_ERR, _("missing ')' in package label: %s\n"), (const char*)keyp);
	    goto exit;
	}
	*t = '\0';
	t = (char *) keyp;
	t = strrchr(t, '.');
	/* Is this a valid ".arch" suffix? */
	if (t != NULL && isArch(t+1)) {
	   *t++ = '\0';
	   arch = t;
	}
    }

    mi = rpmdbInitIterator(ts->rdb, rpmtag, keyp, keylen);

    /* Verify header signature/digest during retrieve (if not disabled). */
    if (mi && !(ts->vsflags & RPMVSF_NOHDRCHK))
	(void) rpmdbSetHdrChk(mi, ts, headerCheck);

    /* Select specified arch only. */
    if (arch != NULL)
	xx = rpmdbSetIteratorRE(mi, RPMTAG_ARCH, RPMMIRE_DEFAULT, arch);

exit:
    free(tmp);

    return mi;
}

rpmKeyring rpmtsGetKeyring(rpmts ts, int autoload)
{
    rpmKeyring keyring = NULL;
    if (ts) {
	if (ts->keyring == NULL && autoload) {
	    loadKeyring(ts);
	}
	keyring = ts->keyring;
    }
    return rpmKeyringLink(keyring);
}

int rpmtsSetKeyring(rpmts ts, rpmKeyring keyring)
{
    /*
     * Should we permit switching keyring on the fly? For now, require
     * rpmdb isn't open yet (fairly arbitrary limitation)...
     */
    if (ts == NULL || rpmtsGetRdb(ts) != NULL)
	return -1;

    rpmKeyringFree(ts->keyring);
    ts->keyring = rpmKeyringLink(keyring);
    return 0;
}

static int loadKeyringFromFiles(rpmts ts)
{
    ARGV_t files = NULL;
    /* XXX TODO: deal with chroot path issues */
    char *pkpath = rpmGetPath(ts->rootDir, "%{_keyringpath}/*.key", NULL);
    int nkeys = 0;

    rpmlog(RPMLOG_DEBUG, "loading keyring from pubkeys in %s\n", pkpath);
    if (rpmGlob(pkpath, NULL, &files)) {
	rpmlog(RPMLOG_DEBUG, "couldn't find any keys in %s\n", pkpath);
	goto exit;
    }

    for (char **f = files; *f; f++) {
	rpmPubkey key = rpmPubkeyRead(*f);
	if (!key) {
	    rpmlog(RPMLOG_ERR, _("%s: reading of public key failed.\n"), *f);
	    continue;
	}
	if (rpmKeyringAddKey(ts->keyring, key) == 0) {
	    nkeys++;
	    rpmlog(RPMLOG_DEBUG, "added key %s to keyring\n", *f);
	}
	rpmPubkeyFree(key);
    }
exit:
    free(pkpath);
    argvFree(files);
    return nkeys;
}

static int loadKeyringFromDB(rpmts ts)
{
    Header h;
    rpmdbMatchIterator mi;
    int nkeys = 0;

    rpmlog(RPMLOG_DEBUG, "loading keyring from rpmdb\n");
    mi = rpmtsInitIterator(ts, RPMTAG_NAME, "gpg-pubkey", 0);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	struct rpmtd_s pubkeys;
	const char *key;

	if (!headerGet(h, RPMTAG_PUBKEYS, &pubkeys, HEADERGET_MINMEM))
	   continue;

	while ((key = rpmtdNextString(&pubkeys))) {
	    uint8_t *pkt;
	    size_t pktlen;

	    if (b64decode(key, (void **) &pkt, &pktlen) == 0) {
		rpmPubkey key = rpmPubkeyNew(pkt, pktlen);
		if (rpmKeyringAddKey(ts->keyring, key) == 0) {
		    char *nvr = headerGetNEVR(h, NULL);
		    rpmlog(RPMLOG_DEBUG, "added key %s to keyring\n", nvr);
		    free(nvr);
		    nkeys++;
		}
		rpmPubkeyFree(key);
		free(pkt);
	    }
	}
	rpmtdFreeData(&pubkeys);
    }
    rpmdbFreeIterator(mi);

    return nkeys;
}

static void loadKeyring(rpmts ts)
{
    ts->keyring = rpmKeyringNew();
    if (loadKeyringFromFiles(ts) == 0) {
	if (loadKeyringFromDB(ts) > 0) {
	    /* XXX make this a warning someday... */
	    rpmlog(RPMLOG_DEBUG, "Using legacy gpg-pubkey(s) from rpmdb\n");
	}
    }
}

rpmRC rpmtsFindPubkey(rpmts ts, pgpDig dig)
{
    rpmRC res = RPMRC_NOKEY;

    if (dig == NULL)
	goto exit;

    if (ts->keyring == NULL) {
	loadKeyring(ts);
    }
    res = rpmKeyringLookup(ts->keyring, dig);

exit:
    return res;
}

/* Build pubkey header. */
static int makePubkeyHeader(rpmts ts, rpmPubkey key, Header h)
{
    const char * afmt = "%{pubkeys:armor}";
    const char * group = "Public Keys";
    const char * license = "pubkey";
    const char * buildhost = "localhost";
    rpmsenseFlags pflags = (RPMSENSE_KEYRING|RPMSENSE_EQUAL);
    uint32_t zero = 0;
    pgpDig dig = NULL;
    pgpDigParams pubp = NULL;
    char * d = NULL;
    char * enc = NULL;
    char * n = NULL;
    char * u = NULL;
    char * v = NULL;
    char * r = NULL;
    char * evr = NULL;
    int rc = -1;

    if ((enc = rpmPubkeyBase64(key)) == NULL)
	goto exit;
    if ((dig = rpmPubkeyDig(key)) == NULL)
	goto exit;

    /* Build header elements. */
    pubp = &dig->pubkey;
    v = pgpHexStr(pubp->signid, sizeof(pubp->signid)); 
    r = pgpHexStr(pubp->time, sizeof(pubp->time));

    rasprintf(&n, "gpg(%s)", v+8);
    rasprintf(&u, "gpg(%s)", pubp->userid ? pubp->userid : "none");
    rasprintf(&evr, "%d:%s-%s", pubp->version, v, r);

    headerPutString(h, RPMTAG_PUBKEYS, enc);

    if ((d = headerFormat(h, afmt, NULL)) == NULL)
	goto exit;

    headerPutString(h, RPMTAG_NAME, "gpg-pubkey");
    headerPutString(h, RPMTAG_VERSION, v+8);
    headerPutString(h, RPMTAG_RELEASE, r);
    headerPutString(h, RPMTAG_DESCRIPTION, d);
    headerPutString(h, RPMTAG_GROUP, group);
    headerPutString(h, RPMTAG_LICENSE, license);
    headerPutString(h, RPMTAG_SUMMARY, u);

    headerPutUint32(h, RPMTAG_SIZE, &zero, 1);

    headerPutString(h, RPMTAG_PROVIDENAME, u);
    headerPutString(h, RPMTAG_PROVIDEVERSION, evr);
    headerPutUint32(h, RPMTAG_PROVIDEFLAGS, &pflags, 1);
	
    headerPutString(h, RPMTAG_PROVIDENAME, n);
    headerPutString(h, RPMTAG_PROVIDEVERSION, evr);
    headerPutUint32(h, RPMTAG_PROVIDEFLAGS, &pflags, 1);

    headerPutString(h, RPMTAG_RPMVERSION, RPMVERSION);
    headerPutString(h, RPMTAG_BUILDHOST, buildhost);
    headerPutString(h, RPMTAG_SOURCERPM, "(none)");

    {   rpm_tid_t tid = rpmtsGetTid(ts);
	headerPutUint32(h, RPMTAG_INSTALLTIME, &tid, 1);
	headerPutUint32(h, RPMTAG_BUILDTIME, &tid, 1);
    }
    rc = 0;

exit:
    pgpFreeDig(dig);
    free(n);
    free(u);
    free(v);
    free(r);
    free(evr);
    free(enc);
    free(d);

    return rc;
}

rpmRC rpmtsImportPubkey(const rpmts ts, const unsigned char * pkt, size_t pktlen)
{
    Header h = headerNew();
    rpmRC rc = RPMRC_FAIL;		/* assume failure */
    rpmPubkey pubkey = NULL;
    rpmKeyring keyring = rpmtsGetKeyring(ts, 1);

    if ((pubkey = rpmPubkeyNew(pkt, pktlen)) == NULL)
	goto exit;
    if (rpmKeyringAddKey(keyring, pubkey) != 0)
	goto exit;
    if (makePubkeyHeader(ts, pubkey, h) != 0) 
	goto exit;

    /* Add header to database. */
    if (rpmtsOpenDB(ts, (O_RDWR|O_CREAT)))
	goto exit;
    if (rpmdbAdd(rpmtsGetRdb(ts), rpmtsGetTid(ts), h, NULL, NULL) != 0)
	goto exit;
    rc = RPMRC_OK;

exit:
    /* Clean up. */
    headerFree(h);
    rpmPubkeyFree(pubkey);
    rpmKeyringFree(keyring);
    return rc;
}

int rpmtsSetSolveCallback(rpmts ts,
		int (*solve) (rpmts ts, rpmds key, const void * data),
		const void * solveData)
{
    int rc = 0;

    if (ts) {
	ts->solve = solve;
	ts->solveData = solveData;
    }
    return rc;
}

rpmps rpmtsProblems(rpmts ts)
{
    rpmps ps = NULL;
    if (ts) {
	if (ts->probs)
	    ps = rpmpsLink(ts->probs, RPMDBG_M("rpmtsProblems"));
    }
    return ps;
}

void rpmtsCleanProblems(rpmts ts)
{
    if (ts && ts->probs) {
	ts->probs = rpmpsFree(ts->probs);
    }
}

void rpmtsClean(rpmts ts)
{
    rpmtsi pi; rpmte p;

    if (ts == NULL)
	return;

    /* Clean up after dependency checks. */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL)
	rpmteCleanDS(p);
    pi = rpmtsiFree(pi);

    ts->addedPackages = rpmalFree(ts->addedPackages);
    ts->numAddedPackages = 0;

    rpmtsCleanProblems(ts);
}

void rpmtsEmpty(rpmts ts)
{
    if (ts == NULL)
	return;

    rpmtsClean(ts);

    for (int oc = 0; oc < ts->orderCount; oc++) {
	ts->order[oc] = rpmteFree(ts->order[oc]);
    }

    ts->orderCount = 0;
    ts->ntrees = 0;
    ts->maxDepth = 0;

    ts->numRemovedPackages = 0;
    return;
}

static void rpmtsPrintStat(const char * name, struct rpmop_s * op)
{
    static const unsigned int scale = (1000 * 1000);
    if (op != NULL && op->count > 0)
	fprintf(stderr, "   %s %6d %6lu.%06lu MB %6lu.%06lu secs\n",
		name, op->count,
		(unsigned long)op->bytes/scale, (unsigned long)op->bytes%scale,
		op->usecs/scale, op->usecs%scale);
}

static void rpmtsPrintStats(rpmts ts)
{
    (void) rpmswExit(rpmtsOp(ts, RPMTS_OP_TOTAL), 0);

    rpmtsPrintStat("total:       ", rpmtsOp(ts, RPMTS_OP_TOTAL));
    rpmtsPrintStat("check:       ", rpmtsOp(ts, RPMTS_OP_CHECK));
    rpmtsPrintStat("order:       ", rpmtsOp(ts, RPMTS_OP_ORDER));
    rpmtsPrintStat("fingerprint: ", rpmtsOp(ts, RPMTS_OP_FINGERPRINT));
    rpmtsPrintStat("install:     ", rpmtsOp(ts, RPMTS_OP_INSTALL));
    rpmtsPrintStat("erase:       ", rpmtsOp(ts, RPMTS_OP_ERASE));
    rpmtsPrintStat("scriptlets:  ", rpmtsOp(ts, RPMTS_OP_SCRIPTLETS));
    rpmtsPrintStat("compress:    ", rpmtsOp(ts, RPMTS_OP_COMPRESS));
    rpmtsPrintStat("uncompress:  ", rpmtsOp(ts, RPMTS_OP_UNCOMPRESS));
    rpmtsPrintStat("digest:      ", rpmtsOp(ts, RPMTS_OP_DIGEST));
    rpmtsPrintStat("signature:   ", rpmtsOp(ts, RPMTS_OP_SIGNATURE));
    rpmtsPrintStat("dbadd:       ", rpmtsOp(ts, RPMTS_OP_DBADD));
    rpmtsPrintStat("dbremove:    ", rpmtsOp(ts, RPMTS_OP_DBREMOVE));
    rpmtsPrintStat("dbget:       ", rpmtsOp(ts, RPMTS_OP_DBGET));
    rpmtsPrintStat("dbput:       ", rpmtsOp(ts, RPMTS_OP_DBPUT));
    rpmtsPrintStat("dbdel:       ", rpmtsOp(ts, RPMTS_OP_DBDEL));
}

rpmts rpmtsFree(rpmts ts)
{
    if (ts == NULL)
	return NULL;

    if (ts->nrefs > 1)
	return rpmtsUnlink(ts, RPMDBG_M("tsCreate"));

    rpmtsEmpty(ts);

    (void) rpmtsCloseDB(ts);

    ts->removedPackages = _free(ts->removedPackages);

    ts->dsi = _free(ts->dsi);

    if (ts->scriptFd != NULL) {
	ts->scriptFd = fdFree(ts->scriptFd, RPMDBG_M("rpmtsFree"));
	ts->scriptFd = NULL;
    }
    ts->rootDir = _free(ts->rootDir);
    ts->currDir = _free(ts->currDir);

    ts->order = _free(ts->order);
    ts->orderAlloced = 0;

    ts->keyring = rpmKeyringFree(ts->keyring);

    if (_rpmts_stats)
	rpmtsPrintStats(ts);

    (void) rpmtsUnlink(ts, RPMDBG_M("tsCreate"));

    ts = _free(ts);

    return NULL;
}

rpmVSFlags rpmtsVSFlags(rpmts ts)
{
    rpmVSFlags vsflags = 0;
    if (ts != NULL)
	vsflags = ts->vsflags;
    return vsflags;
}

rpmVSFlags rpmtsSetVSFlags(rpmts ts, rpmVSFlags vsflags)
{
    rpmVSFlags ovsflags = 0;
    if (ts != NULL) {
	ovsflags = ts->vsflags;
	ts->vsflags = vsflags;
    }
    return ovsflags;
}

int rpmtsUnorderedSuccessors(rpmts ts, int first)
{
    int unorderedSuccessors = 0;
    if (ts != NULL) {
	unorderedSuccessors = ts->unorderedSuccessors;
	if (first >= 0)
	    ts->unorderedSuccessors = first;
    }
    return unorderedSuccessors;
}

const char * rpmtsRootDir(rpmts ts)
{
    const char * rootDir = NULL;

    if (ts != NULL && ts->rootDir != NULL) {
	urltype ut = urlPath(ts->rootDir, &rootDir);
	switch (ut) {
	case URL_IS_UNKNOWN:
	case URL_IS_PATH:
	    break;
	/* XXX these shouldn't be allowed as rootdir! */
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_HKP:
	case URL_IS_FTP:
	case URL_IS_DASH:
	default:
	    rootDir = "/";
	    break;
	}
    }
    return rootDir;
}

int rpmtsSetRootDir(rpmts ts, const char * rootDir)
{
    if (ts == NULL || (rootDir && rootDir[0] != '/')) {
	return -1;
    }

    ts->rootDir = _free(ts->rootDir);
    /* Ensure clean path with a trailing slash */
    ts->rootDir = rootDir ? rpmGetPath(rootDir, NULL) : xstrdup("/");
    if (strcmp(ts->rootDir, "/") != 0) {
	rstrcat(&ts->rootDir, "/");
    }
    return 0;
}

const char * rpmtsCurrDir(rpmts ts)
{
    const char * currDir = NULL;
    if (ts != NULL) {
	currDir = ts->currDir;
    }
    return currDir;
}

void rpmtsSetCurrDir(rpmts ts, const char * currDir)
{
    if (ts != NULL) {
	ts->currDir = _free(ts->currDir);
	if (currDir)
	    ts->currDir = xstrdup(currDir);
    }
}

FD_t rpmtsScriptFd(rpmts ts)
{
    FD_t scriptFd = NULL;
    if (ts != NULL) {
	scriptFd = ts->scriptFd;
    }
    return scriptFd;
}

void rpmtsSetScriptFd(rpmts ts, FD_t scriptFd)
{

    if (ts != NULL) {
	if (ts->scriptFd != NULL) {
	    ts->scriptFd = fdFree(ts->scriptFd, 
				  RPMDBG_M("rpmtsSetScriptFd"));
	    ts->scriptFd = NULL;
	}
	if (scriptFd != NULL)
	    ts->scriptFd = fdLink((void *)scriptFd, 
				  RPMDBG_M("rpmtsSetScriptFd"));
    }
}

int rpmtsSELinuxEnabled(rpmts ts)
{
    return (ts != NULL ? (ts->selinuxEnabled > 0) : 0);
}

int rpmtsChrootDone(rpmts ts)
{
    return (ts != NULL ? ts->chrootDone : 0);
}

int rpmtsSetChrootDone(rpmts ts, int chrootDone)
{
    int ochrootDone = 0;
    if (ts != NULL) {
	ochrootDone = ts->chrootDone;
	rpmdbSetChrootDone(rpmtsGetRdb(ts), chrootDone);
	ts->chrootDone = chrootDone;
    }
    return ochrootDone;
}

rpm_tid_t rpmtsGetTid(rpmts ts)
{
    rpm_tid_t tid = -1;  /* XXX -1 is time(2) error return. */
    if (ts != NULL) {
	tid = ts->tid;
    }
    return tid;
}

rpm_tid_t rpmtsSetTid(rpmts ts, rpm_tid_t tid)
{
    rpm_tid_t otid = -1; /* XXX -1 is time(2) error return. */
    if (ts != NULL) {
	otid = ts->tid;
	ts->tid = tid;
    }
    return otid;
}

rpmdb rpmtsGetRdb(rpmts ts)
{
    rpmdb rdb = NULL;
    if (ts != NULL) {
	rdb = ts->rdb;
    }
    return rdb;
}

int rpmtsInitDSI(const rpmts ts)
{
    rpmDiskSpaceInfo dsi;
    struct stat sb;
    int rc;
    int i;

    if (rpmtsFilterFlags(ts) & RPMPROB_FILTER_DISKSPACE)
	return 0;

    rpmlog(RPMLOG_DEBUG, "mounted filesystems:\n");
    rpmlog(RPMLOG_DEBUG,
	"    i        dev    bsize       bavail       iavail mount point\n");

    rc = rpmGetFilesystemList(&ts->filesystems, &ts->filesystemCount);
    if (rc || ts->filesystems == NULL || ts->filesystemCount <= 0)
	return rc;

    /* Get available space on mounted file systems. */

    ts->dsi = _free(ts->dsi);
    ts->dsi = xcalloc((ts->filesystemCount + 1), sizeof(*ts->dsi));

    dsi = ts->dsi;

    if (dsi != NULL)
    for (i = 0; (i < ts->filesystemCount) && dsi; i++, dsi++) {
#if STATFS_IN_SYS_STATVFS
	struct statvfs sfb;
	memset(&sfb, 0, sizeof(sfb));
	rc = statvfs(ts->filesystems[i], &sfb);
#else
	struct statfs sfb;
	memset(&sfb, 0, sizeof(sfb));
#  if STAT_STATFS4
/* This platform has the 4-argument version of the statfs call.  The last two
 * should be the size of struct statfs and 0, respectively.  The 0 is the
 * filesystem type, and is always 0 when statfs is called on a mounted
 * filesystem, as we're doing.
 */
	rc = statfs(ts->filesystems[i], &sfb, sizeof(sfb), 0);
#  else
	rc = statfs(ts->filesystems[i], &sfb);
#  endif
#endif
	if (rc)
	    break;

	rc = stat(ts->filesystems[i], &sb);
	if (rc)
	    break;
	dsi->dev = sb.st_dev;

	dsi->bsize = sfb.f_bsize;
	dsi->bneeded = 0;
	dsi->ineeded = 0;
#ifdef STATFS_HAS_F_BAVAIL
	dsi->bavail = sfb.f_bavail;
#else
/* FIXME: the statfs struct doesn't have a member to tell how many blocks are
 * available for non-superusers.  f_blocks - f_bfree is probably too big, but
 * it's about all we can do.
 */
	dsi->bavail = sfb.f_blocks - sfb.f_bfree;
#endif
	/* XXX Avoid FAT and other file systems that have not inodes. */
	/* XXX assigning negative value to unsigned type */
	dsi->iavail = !(sfb.f_ffree == 0 && sfb.f_files == 0)
				? sfb.f_ffree : -1;
	rpmlog(RPMLOG_DEBUG, 
		"%5d 0x%08x %8" PRId64 " %12" PRId64 " %12" PRId64" %s\n",
		i, (unsigned) dsi->dev, dsi->bsize,
		dsi->bavail, dsi->iavail,
		ts->filesystems[i]);
    }
    return rc;
}

void rpmtsUpdateDSI(const rpmts ts, dev_t dev,
		rpm_loff_t fileSize, rpm_loff_t prevSize, rpm_loff_t fixupSize,
		rpmFileAction action)
{
    rpmDiskSpaceInfo dsi;
    int64_t bneeded;

    dsi = ts->dsi;
    if (dsi) {
	while (dsi->bsize && dsi->dev != dev)
	    dsi++;
	if (dsi->bsize == 0)
	    dsi = NULL;
    }
    if (dsi == NULL)
	return;

    bneeded = BLOCK_ROUND(fileSize, dsi->bsize);

    switch (action) {
    case FA_BACKUP:
    case FA_SAVE:
    case FA_ALTNAME:
	dsi->ineeded++;
	dsi->bneeded += bneeded;
	break;

    /*
     * FIXME: If two packages share a file (same md5sum), and
     * that file is being replaced on disk, will dsi->bneeded get
     * adjusted twice? Quite probably!
     */
    case FA_CREATE:
	dsi->bneeded += bneeded;
	dsi->bneeded -= BLOCK_ROUND(prevSize, dsi->bsize);
	break;

    case FA_ERASE:
	dsi->ineeded--;
	dsi->bneeded -= bneeded;
	break;

    default:
	break;
    }

    if (fixupSize)
	dsi->bneeded -= BLOCK_ROUND(fixupSize, dsi->bsize);
}

void rpmtsCheckDSIProblems(const rpmts ts, const rpmte te)
{
    rpmDiskSpaceInfo dsi;
    rpmps ps;
    int fc;
    int i;

    if (ts->filesystems == NULL || ts->filesystemCount <= 0)
	return;

    dsi = ts->dsi;
    if (dsi == NULL)
	return;
    fc = rpmfiFC( rpmteFI(te, RPMTAG_BASENAMES) );
    if (fc <= 0)
	return;

    ps = rpmtsProblems(ts);
    for (i = 0; i < ts->filesystemCount; i++, dsi++) {

	if (dsi->bavail >= 0 && adj_fs_blocks(dsi->bneeded) > dsi->bavail) {
	    rpmpsAppend(ps, RPMPROB_DISKSPACE,
			rpmteNEVRA(te), rpmteKey(te),
			ts->filesystems[i], NULL, NULL,
 	   (adj_fs_blocks(dsi->bneeded) - dsi->bavail) * dsi->bsize);
	}

	if (dsi->iavail >= 0 && adj_fs_blocks(dsi->ineeded) > dsi->iavail) {
	    rpmpsAppend(ps, RPMPROB_DISKNODES,
			rpmteNEVRA(te), rpmteKey(te),
			ts->filesystems[i], NULL, NULL,
 	    (adj_fs_blocks(dsi->ineeded) - dsi->iavail));
	}
    }
    ps = rpmpsFree(ps);
}

void * rpmtsNotify(rpmts ts, rpmte te,
		rpmCallbackType what, rpm_loff_t amount, rpm_loff_t total)
{
    void * ptr = NULL;
    if (ts && ts->notify) {
	Header h = NULL;
	fnpyKey cbkey = NULL;
	if (te) {
	    h = rpmteHeader(te);
	    cbkey = rpmteKey(te);
	}
	ptr = ts->notify(h, what, amount, total, cbkey, ts->notifyData);

	if (h) {
	    headerUnlink(h); /* undo rpmteHeader() ref */
	}
    }
    return ptr;
}

int rpmtsNElements(rpmts ts)
{
    int nelements = 0;
    if (ts != NULL && ts->order != NULL) {
	nelements = ts->orderCount;
    }
    return nelements;
}

rpmte rpmtsElement(rpmts ts, int ix)
{
    rpmte te = NULL;
    if (ts != NULL && ts->order != NULL) {
	if (ix >= 0 && ix < ts->orderCount)
	    te = ts->order[ix];
    }
    return te;
}

rpmprobFilterFlags rpmtsFilterFlags(rpmts ts)
{
    return (ts != NULL ? ts->ignoreSet : 0);
}

rpmtransFlags rpmtsFlags(rpmts ts)
{
    return (ts != NULL ? ts->transFlags : 0);
}

rpmtransFlags rpmtsSetFlags(rpmts ts, rpmtransFlags transFlags)
{
    rpmtransFlags otransFlags = 0;
    if (ts != NULL) {
	otransFlags = ts->transFlags;
	ts->transFlags = transFlags;
    }
    return otransFlags;
}

rpmSpec rpmtsSpec(rpmts ts)
{
    return ts->spec;
}

rpmSpec rpmtsSetSpec(rpmts ts, rpmSpec spec)
{
    rpmSpec ospec = ts->spec;
    ts->spec = spec;
    return ospec;
}

rpmte rpmtsRelocateElement(rpmts ts)
{
    return ts->relocateElement;
}

rpmte rpmtsSetRelocateElement(rpmts ts, rpmte relocateElement)
{
    rpmte orelocateElement = ts->relocateElement;
    ts->relocateElement = relocateElement;
    return orelocateElement;
}

rpm_color_t rpmtsColor(rpmts ts)
{
    return (ts != NULL ? ts->color : 0);
}

rpm_color_t rpmtsSetColor(rpmts ts, rpm_color_t color)
{
    rpm_color_t ocolor = 0;
    if (ts != NULL) {
	ocolor = ts->color;
	ts->color = color;
    }
    return ocolor;
}

rpm_color_t rpmtsPrefColor(rpmts ts)
{
    return (ts != NULL ? ts->prefcolor : 0);
}

rpmop rpmtsOp(rpmts ts, rpmtsOpX opx)
{
    rpmop op = NULL;

    if (ts != NULL && opx >= 0 && opx < RPMTS_OP_MAX)
	op = ts->ops + opx;
    return op;
}

int rpmtsSetNotifyCallback(rpmts ts,
		rpmCallbackFunction notify, rpmCallbackData notifyData)
{
    if (ts != NULL) {
	ts->notify = notify;
	ts->notifyData = notifyData;
    }
    return 0;
}

int rpmtsGetKeys(const rpmts ts, fnpyKey ** ep, int * nep)
{
    int rc = 0;

    if (nep) *nep = ts->orderCount;
    if (ep) {
	rpmtsi pi;	rpmte p;
	fnpyKey * e;

	*ep = e = xmalloc(ts->orderCount * sizeof(*e));
	pi = rpmtsiInit(ts);
	while ((p = rpmtsiNext(pi, 0)) != NULL) {
	    switch (rpmteType(p)) {
	    case TR_ADDED:
		*e = rpmteKey(p);
		break;
	    case TR_REMOVED:
	    default:
		*e = NULL;
		break;
	    }
	    e++;
	}
	pi = rpmtsiFree(pi);
    }
    return rc;
}

rpmts rpmtsCreate(void)
{
    rpmts ts;

    ts = xcalloc(1, sizeof(*ts));
    memset(&ts->ops, 0, sizeof(ts->ops));
    (void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_TOTAL), -1);
    ts->filesystemCount = 0;
    ts->filesystems = NULL;
    ts->dsi = NULL;

    ts->solve = NULL;
    ts->solveData = NULL;

    ts->rdb = NULL;
    ts->dbmode = O_RDONLY;

    ts->scriptFd = NULL;
    ts->tid = (rpm_tid_t) time(NULL);
    ts->delta = 5;

    ts->color = rpmExpandNumeric("%{?_transaction_color}");
    ts->prefcolor = rpmExpandNumeric("%{?_prefer_color}")?:2;

    ts->numRemovedPackages = 0;
    ts->allocedRemovedPackages = ts->delta;
    ts->removedPackages = xcalloc(ts->allocedRemovedPackages,
			sizeof(*ts->removedPackages));

    ts->rootDir = NULL;
    ts->currDir = NULL;
    ts->chrootDone = 0;

    ts->selinuxEnabled = is_selinux_enabled();

    ts->numAddedPackages = 0;
    ts->addedPackages = NULL;

    ts->orderAlloced = 0;
    ts->orderCount = 0;
    ts->order = NULL;
    ts->ntrees = 0;
    ts->maxDepth = 0;

    ts->probs = NULL;

    ts->keyring = NULL;

    ts->nrefs = 0;

    /* make sure crypto gets initialized before we might go chrooting */
    rpmInitCrypto();

    return rpmtsLink(ts, RPMDBG_M("tsCreate"));
}

