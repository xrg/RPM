/** \ingroup rpmcli
 * \file lib/rpminstall.c
 */

#include "system.h"

#include <rpm/rpmcli.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmlib.h>		/* rpmReadPackageFile, vercmp etc */
#include <rpm/rpmdb.h>
#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmfileutil.h>

#include "lib/manifest.h"
#include "debug.h"

int rpmcliPackagesTotal = 0;
int rpmcliHashesCurrent = 0;
int rpmcliHashesTotal = 0;
int rpmcliProgressCurrent = 0;
int rpmcliProgressTotal = 0;

/**
 * Print a CLI progress bar.
 * @todo Unsnarl isatty(STDOUT_FILENO) from the control flow.
 * @param amount	current
 * @param total		final
 */
static void printHash(const rpm_loff_t amount, const rpm_loff_t total)
{
    int hashesNeeded;

    rpmcliHashesTotal = (isatty (STDOUT_FILENO) ? 44 : 50);

    if (rpmcliHashesCurrent != rpmcliHashesTotal) {
	float pct = (total ? (((float) amount) / total) : 1.0);
	hashesNeeded = (rpmcliHashesTotal * pct) + 0.5;
	while (hashesNeeded > rpmcliHashesCurrent) {
	    if (isatty (STDOUT_FILENO)) {
		int i;
		for (i = 0; i < rpmcliHashesCurrent; i++)
		    (void) putchar ('#');
		for (; i < rpmcliHashesTotal; i++)
		    (void) putchar (' ');
		fprintf(stdout, "(%3d%%)", (int)((100 * pct) + 0.5));
		for (i = 0; i < (rpmcliHashesTotal + 6); i++)
		    (void) putchar ('\b');
	    } else
		fprintf(stdout, "#");

	    rpmcliHashesCurrent++;
	}
	(void) fflush(stdout);

	if (rpmcliHashesCurrent == rpmcliHashesTotal) {
	    int i;
	    rpmcliProgressCurrent++;
	    if (isatty(STDOUT_FILENO)) {
	        for (i = 1; i < rpmcliHashesCurrent; i++)
		    (void) putchar ('#');
		pct = (rpmcliProgressTotal
		    ? (((float) rpmcliProgressCurrent) / rpmcliProgressTotal)
		    : 1);
		fprintf(stdout, " [%3d%%]", (int)((100 * pct) + 0.5));
	    }
	    fprintf(stdout, "\n");
	}
	(void) fflush(stdout);
    }
}

void * rpmShowProgress(const void * arg,
			const rpmCallbackType what,
			const rpm_loff_t amount,
			const rpm_loff_t total,
			fnpyKey key,
			void * data)
{
    Header h = (Header) arg;
    char * s;
    int flags = (int) ((long)data);
    void * rc = NULL;
    const char * filename = (const char *)key;
    static FD_t fd = NULL;
    int xx;

    switch (what) {
    case RPMCALLBACK_INST_OPEN_FILE:
	if (filename == NULL || filename[0] == '\0')
	    return NULL;
	fd = Fopen(filename, "r.ufdio");
	/* FIX: still necessary? */
	if (fd == NULL || Ferror(fd)) {
	    rpmlog(RPMLOG_ERR, _("open of %s failed: %s\n"), filename,
			Fstrerror(fd));
	    if (fd != NULL) {
		xx = Fclose(fd);
		fd = NULL;
	    }
	} else
	    fd = fdLink(fd, RPMDBG_M("persist (showProgress)"));
	return (void *)fd;
	break;

    case RPMCALLBACK_INST_CLOSE_FILE:
	/* FIX: still necessary? */
	fd = fdFree(fd, RPMDBG_M("persist (showProgress)"));
	if (fd != NULL) {
	    xx = Fclose(fd);
	    fd = NULL;
	}
	break;

    case RPMCALLBACK_INST_START:
	rpmcliHashesCurrent = 0;
	if (h == NULL || !(flags & INSTALL_LABEL))
	    break;
	/* @todo Remove headerFormat() on a progress callback. */
	if (flags & INSTALL_HASH) {
	    s = headerFormat(h, "%{NAME}", NULL);
	    if (isatty (STDOUT_FILENO))
		fprintf(stdout, "%4d:%-23.23s", rpmcliProgressCurrent + 1, s);
	    else
		fprintf(stdout, "%-28.28s", s);
	    (void) fflush(stdout);
	    s = _free(s);
	} else {
	    s = headerFormat(h, "%{NAME}-%{VERSION}-%{RELEASE}", NULL);
	    fprintf(stdout, "%s\n", s);
	    (void) fflush(stdout);
	    s = _free(s);
	}
	break;

    case RPMCALLBACK_TRANS_PROGRESS:
    case RPMCALLBACK_INST_PROGRESS:
	if (flags & INSTALL_PERCENT)
	    fprintf(stdout, "%%%% %f\n", (double) (total
				? ((((float) amount) / total) * 100)
				: 100.0));
	else if (flags & INSTALL_HASH)
	    printHash(amount, total);
	(void) fflush(stdout);
	break;

    case RPMCALLBACK_TRANS_START:
	rpmcliHashesCurrent = 0;
	rpmcliProgressTotal = 1;
	rpmcliProgressCurrent = 0;
	if (!(flags & INSTALL_LABEL))
	    break;
	if (flags & INSTALL_HASH)
	    fprintf(stdout, "%-28s", _("Preparing..."));
	else
	    fprintf(stdout, "%s\n", _("Preparing packages for installation..."));
	(void) fflush(stdout);
	break;

    case RPMCALLBACK_TRANS_STOP:
	if (flags & INSTALL_HASH)
	    printHash(1, 1);	/* Fixes "preparing..." progress bar */
	rpmcliProgressTotal = rpmcliPackagesTotal;
	rpmcliProgressCurrent = 0;
	break;

    case RPMCALLBACK_UNINST_PROGRESS:
	break;
    case RPMCALLBACK_UNINST_START:
	break;
    case RPMCALLBACK_UNINST_STOP:
	break;
    case RPMCALLBACK_UNPACK_ERROR:
	break;
    case RPMCALLBACK_CPIO_ERROR:
	break;
    case RPMCALLBACK_SCRIPT_ERROR:
	break;
    case RPMCALLBACK_UNKNOWN:
    default:
	break;
    }

    return rc;
}	

struct rpmEIU {
    Header h;
    FD_t fd;
    int numFailed;
    int numPkgs;
    char ** pkgURL;
    char ** fnp;
    char * pkgState;
    int prevx;
    int pkgx;
    int numRPMS;
    int numSRPMS;
    char ** sourceURL;
    int isSource;
    int argc;
    char ** argv;
    rpmRelocation * relocations;
    rpmRC rpmrc;
};

/** @todo Generalize --freshen policies. */
int rpmInstall(rpmts ts, struct rpmInstallArguments_s * ia, ARGV_t fileArgv)
{
    struct rpmEIU * eiu = xcalloc(1, sizeof(*eiu));
    rpmps ps;
    rpmprobFilterFlags probFilter;
    rpmRelocation * relocations;
    char * fileURL = NULL;
    int stopInstall = 0;
    rpmVSFlags vsflags, ovsflags, tvsflags;
    int rc;
    int xx;
    int i;

    if (fileArgv == NULL) goto exit;

    rpmcliPackagesTotal = 0;

    (void) rpmtsSetFlags(ts, ia->transFlags);

    probFilter = ia->probFilter;
    relocations = ia->relocations;

    if (ia->installInterfaceFlags & INSTALL_UPGRADE)
	vsflags = rpmExpandNumeric("%{?_vsflags_erase}");
    else
	vsflags = rpmExpandNumeric("%{?_vsflags_install}");
    if (ia->qva_flags & VERIFY_DIGEST)
	vsflags |= _RPMVSF_NODIGESTS;
    if (ia->qva_flags & VERIFY_SIGNATURE)
	vsflags |= _RPMVSF_NOSIGNATURES;
    if (ia->qva_flags & VERIFY_HDRCHK)
	vsflags |= RPMVSF_NOHDRCHK;
    ovsflags = rpmtsSetVSFlags(ts, (vsflags | RPMVSF_NEEDPAYLOAD));

    {	int notifyFlags;
	notifyFlags = ia->installInterfaceFlags | (rpmIsVerbose() ? INSTALL_LABEL : 0 );
	xx = rpmtsSetNotifyCallback(ts,
			rpmShowProgress, (void *) ((long)notifyFlags));
    }

    if ((eiu->relocations = relocations) != NULL) {
	while (eiu->relocations->oldPath)
	    eiu->relocations++;
	if (eiu->relocations->newPath == NULL)
	    eiu->relocations = NULL;
    }

    /* Build fully globbed list of arguments in argv[argc]. */
    for (eiu->fnp = fileArgv; *eiu->fnp != NULL; eiu->fnp++) {
    	ARGV_t av = NULL;
    	int ac = 0;
	char * fn;

	fn = rpmEscapeSpaces(*eiu->fnp);
	rc = rpmGlob(fn, &ac, &av);
	fn = _free(fn);
	if (rc || ac == 0) {
	    rpmlog(RPMLOG_ERR, _("File not found by glob: %s\n"), *eiu->fnp);
	    eiu->numFailed++;
	    continue;
	}

	argvAppend(&(eiu->argv), av);
	argvFree(av);
	eiu->argc += ac;
    }

restart:
    /* Allocate sufficient storage for next set of args. */
    if (eiu->pkgx >= eiu->numPkgs) {
	eiu->numPkgs = eiu->pkgx + eiu->argc;
	eiu->pkgURL = xrealloc(eiu->pkgURL,
			(eiu->numPkgs + 1) * sizeof(*eiu->pkgURL));
	memset(eiu->pkgURL + eiu->pkgx, 0,
			((eiu->argc + 1) * sizeof(*eiu->pkgURL)));
	eiu->pkgState = xrealloc(eiu->pkgState,
			(eiu->numPkgs + 1) * sizeof(*eiu->pkgState));
	memset(eiu->pkgState + eiu->pkgx, 0,
			((eiu->argc + 1) * sizeof(*eiu->pkgState)));
    }

    /* Retrieve next set of args, cache on local storage. */
    for (i = 0; i < eiu->argc; i++) {
	fileURL = _free(fileURL);
	fileURL = eiu->argv[i];
	eiu->argv[i] = NULL;

	switch (urlIsURL(fileURL)) {
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_FTP:
	{   char *tfn;
	    FD_t tfd;

	    if (rpmIsVerbose())
		fprintf(stdout, _("Retrieving %s\n"), fileURL);

	    tfd = rpmMkTempFile(rpmtsRootDir(ts), &tfn);
	    if (tfd && tfn) {
		Fclose(tfd);
	    	rc = urlGetFile(fileURL, tfn);
	    } else {
		rc = -1;
	    }

	    if (rc != 0) {
		rpmlog(RPMLOG_ERR,
			_("skipping %s - transfer failed\n"), fileURL);
		eiu->numFailed++;
		eiu->pkgURL[eiu->pkgx] = NULL;
		tfn = _free(tfn);
		break;
	    }
	    eiu->pkgState[eiu->pkgx] = 1;
	    eiu->pkgURL[eiu->pkgx] = tfn;
	    eiu->pkgx++;
	}   break;
	case URL_IS_PATH:
	case URL_IS_DASH:	/* WRONG WRONG WRONG */
	case URL_IS_HKP:	/* WRONG WRONG WRONG */
	default:
	    eiu->pkgURL[eiu->pkgx] = fileURL;
	    fileURL = NULL;
	    eiu->pkgx++;
	    break;
	}
    }
    fileURL = _free(fileURL);

    if (eiu->numFailed) goto exit;

    /* Continue processing file arguments, building transaction set. */
    for (eiu->fnp = eiu->pkgURL+eiu->prevx;
	 *eiu->fnp != NULL;
	 eiu->fnp++, eiu->prevx++)
    {
	const char * fileName;

	rpmlog(RPMLOG_DEBUG, "============== %s\n", *eiu->fnp);
	(void) urlPath(*eiu->fnp, &fileName);

	/* Try to read the header from a package file. */
	eiu->fd = Fopen(*eiu->fnp, "r.ufdio");
	if (eiu->fd == NULL || Ferror(eiu->fd)) {
	    rpmlog(RPMLOG_ERR, _("open of %s failed: %s\n"), *eiu->fnp,
			Fstrerror(eiu->fd));
	    if (eiu->fd != NULL) {
		xx = Fclose(eiu->fd);
		eiu->fd = NULL;
	    }
	    eiu->numFailed++; *eiu->fnp = NULL;
	    continue;
	}

	/* Read the header, verifying signatures (if present). */
	tvsflags = rpmtsSetVSFlags(ts, vsflags);
	eiu->rpmrc = rpmReadPackageFile(ts, eiu->fd, *eiu->fnp, &eiu->h);
	tvsflags = rpmtsSetVSFlags(ts, tvsflags);
	xx = Fclose(eiu->fd);
	eiu->fd = NULL;

	switch (eiu->rpmrc) {
	case RPMRC_FAIL:
	    rpmlog(RPMLOG_ERR, _("%s cannot be installed\n"), *eiu->fnp);
	    eiu->numFailed++; *eiu->fnp = NULL;
	    continue;
	    break;
	case RPMRC_NOTFOUND:
	    goto maybe_manifest;
	    break;
	case RPMRC_NOTTRUSTED:
	case RPMRC_NOKEY:
	case RPMRC_OK:
	default:
	    break;
	}

	eiu->isSource = headerIsSource(eiu->h);

	if (eiu->isSource) {
	    rpmlog(RPMLOG_DEBUG, "\tadded source package [%d]\n",
		eiu->numSRPMS);
	    eiu->sourceURL = xrealloc(eiu->sourceURL,
				(eiu->numSRPMS + 2) * sizeof(*eiu->sourceURL));
	    eiu->sourceURL[eiu->numSRPMS] = *eiu->fnp;
	    *eiu->fnp = NULL;
	    eiu->numSRPMS++;
	    eiu->sourceURL[eiu->numSRPMS] = NULL;
	    continue;
	}

	if (eiu->relocations) {
	    struct rpmtd_s prefixes;

	    headerGet(eiu->h, RPMTAG_PREFIXES, &prefixes, HEADERGET_DEFAULT);
	    if (rpmtdCount(&prefixes) == 1) {
		eiu->relocations->oldPath = xstrdup(rpmtdGetString(&prefixes));
		rpmtdFreeData(&prefixes);
	    } else {
		const char * name;
		xx = headerNVR(eiu->h, &name, NULL, NULL);
		rpmlog(RPMLOG_ERR,
			       _("package %s is not relocatable\n"), name);
		eiu->numFailed++;
		goto exit;
	    }
	}

	/* On --freshen, verify package is installed and newer */
	if (ia->installInterfaceFlags & INSTALL_FRESHEN) {
	    rpmdbMatchIterator mi;
	    const char * name;
	    Header oldH;
	    int count;

	    xx = headerNVR(eiu->h, &name, NULL, NULL);
	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, name, 0);
	    count = rpmdbGetIteratorCount(mi);
	    while ((oldH = rpmdbNextIterator(mi)) != NULL) {
		if (rpmVersionCompare(oldH, eiu->h) < 0)
		    continue;
		/* same or newer package already installed */
		count = 0;
		break;
	    }
	    mi = rpmdbFreeIterator(mi);
	    if (count == 0) {
		eiu->h = headerFree(eiu->h);
		continue;
	    }
	    /* Package is newer than those currently installed. */
	}

	rc = rpmtsAddInstallElement(ts, eiu->h, (fnpyKey)fileName,
			(ia->installInterfaceFlags & INSTALL_UPGRADE) != 0,
			relocations);

	/* XXX reference held by transaction set */
	eiu->h = headerFree(eiu->h);
	if (eiu->relocations)
	    eiu->relocations->oldPath = _free(eiu->relocations->oldPath);

	switch(rc) {
	case 0:
	    rpmlog(RPMLOG_DEBUG, "\tadded binary package [%d]\n",
			eiu->numRPMS);
	    break;
	case 1:
	    rpmlog(RPMLOG_ERR,
			    _("error reading from file %s\n"), *eiu->fnp);
	    eiu->numFailed++;
	    goto exit;
	    break;
	case 2:
	    rpmlog(RPMLOG_ERR,
			    _("file %s requires a newer version of RPM\n"),
			    *eiu->fnp);
	    eiu->numFailed++;
	    goto exit;
	    break;
	default:
	    eiu->numFailed++;
	    goto exit;
	    break;
	}

	eiu->numRPMS++;
	continue;

maybe_manifest:
	/* Try to read a package manifest. */
	eiu->fd = Fopen(*eiu->fnp, "r.fpio");
	if (eiu->fd == NULL || Ferror(eiu->fd)) {
	    rpmlog(RPMLOG_ERR, _("open of %s failed: %s\n"), *eiu->fnp,
			Fstrerror(eiu->fd));
	    if (eiu->fd != NULL) {
		xx = Fclose(eiu->fd);
		eiu->fd = NULL;
	    }
	    eiu->numFailed++; *eiu->fnp = NULL;
	    break;
	}

	/* Read list of packages from manifest. */
/* FIX: *eiu->argv can be NULL */
	rc = rpmReadPackageManifest(eiu->fd, &eiu->argc, &eiu->argv);
	if (rc != RPMRC_OK)
	    rpmlog(RPMLOG_ERR, _("%s: not an rpm package (or package manifest): %s\n"),
			*eiu->fnp, Fstrerror(eiu->fd));
	xx = Fclose(eiu->fd);
	eiu->fd = NULL;

	/* If successful, restart the query loop. */
	if (rc == RPMRC_OK) {
	    eiu->prevx++;
	    goto restart;
	}

	eiu->numFailed++; *eiu->fnp = NULL;
	break;
    }

    rpmlog(RPMLOG_DEBUG, "found %d source and %d binary packages\n",
		eiu->numSRPMS, eiu->numRPMS);

    if (eiu->numFailed) goto exit;

    if (eiu->numRPMS && !(ia->installInterfaceFlags & INSTALL_NODEPS)) {

	if (rpmtsCheck(ts)) {
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;
	}

	ps = rpmtsProblems(ts);
	if (!stopInstall && rpmpsNumProblems(ps) > 0) {
	    rpmlog(RPMLOG_ERR, _("Failed dependencies:\n"));
	    rpmpsPrint(NULL, ps);
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;
	}
	ps = rpmpsFree(ps);
    }

    if (eiu->numRPMS && !(ia->installInterfaceFlags & INSTALL_NOORDER)) {
	if (rpmtsOrder(ts)) {
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;
	}
    }

    if (eiu->numRPMS && !stopInstall) {

	rpmcliPackagesTotal += eiu->numSRPMS;

	rpmlog(RPMLOG_DEBUG, "installing binary packages\n");

	/* Drop added/available package indices and dependency sets. */
	rpmtsClean(ts);

	rc = rpmtsRun(ts, NULL, probFilter);
	ps = rpmtsProblems(ts);

	if (rc < 0) {
	    eiu->numFailed += eiu->numRPMS;
	} else if (rc > 0) {
	    eiu->numFailed += rc;
	    if (rpmpsNumProblems(ps) > 0)
		rpmpsPrint(stderr, ps);
	}
	ps = rpmpsFree(ps);
    }

    if (eiu->numSRPMS && !stopInstall) {
	if (eiu->sourceURL != NULL)
	for (i = 0; i < eiu->numSRPMS; i++) {
	    rpmdbCheckSignals();
	    if (eiu->sourceURL[i] == NULL) continue;
	    eiu->fd = Fopen(eiu->sourceURL[i], "r.ufdio");
	    if (eiu->fd == NULL || Ferror(eiu->fd)) {
		rpmlog(RPMLOG_ERR, _("cannot open file %s: %s\n"),
			   eiu->sourceURL[i], Fstrerror(eiu->fd));
		if (eiu->fd != NULL) {
		    xx = Fclose(eiu->fd);
		    eiu->fd = NULL;
		}
		continue;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)) {
		eiu->rpmrc = rpmInstallSourcePackage(ts, eiu->fd, NULL, NULL);
		if (eiu->rpmrc != RPMRC_OK) eiu->numFailed++;
	    }

	    xx = Fclose(eiu->fd);
	    eiu->fd = NULL;
	}
    }

exit:
    if (eiu->pkgURL != NULL)
    for (i = 0; i < eiu->numPkgs; i++) {
	if (eiu->pkgURL[i] == NULL) continue;
	if (eiu->pkgState[i] == 1)
	    (void) unlink(eiu->pkgURL[i]);
	eiu->pkgURL[i] = _free(eiu->pkgURL[i]);
    }
    eiu->pkgState = _free(eiu->pkgState);
    eiu->pkgURL = _free(eiu->pkgURL);
    eiu->argv = _free(eiu->argv);
    rc = eiu->numFailed;
    free(eiu);

    rpmtsEmpty(ts);

    return rc;
}

int rpmErase(rpmts ts, struct rpmInstallArguments_s * ia, ARGV_const_t argv)
{
    char * const * arg;
    char *qfmt = NULL;
    int numFailed = 0;
    int stopUninstall = 0;
    int numPackages = 0;
    rpmVSFlags vsflags, ovsflags;
    rpmps ps;

    if (argv == NULL) return 0;

    vsflags = rpmExpandNumeric("%{?_vsflags_erase}");
    if (ia->qva_flags & VERIFY_DIGEST)
	vsflags |= _RPMVSF_NODIGESTS;
    if (ia->qva_flags & VERIFY_SIGNATURE)
	vsflags |= _RPMVSF_NOSIGNATURES;
    if (ia->qva_flags & VERIFY_HDRCHK)
	vsflags |= RPMVSF_NOHDRCHK;
    ovsflags = rpmtsSetVSFlags(ts, vsflags);

    /* XXX suggest mechanism only meaningful when installing */
    ia->transFlags |= RPMTRANS_FLAG_NOSUGGEST;

    (void) rpmtsSetFlags(ts, ia->transFlags);

#ifdef	NOTYET	/* XXX no callbacks on erase yet */
    {	int notifyFlags, xx;
	notifyFlags = ia->eraseInterfaceFlags | (rpmIsVerbose() ? INSTALL_LABEL : 0 );
	xx = rpmtsSetNotifyCallback(ts,
			rpmShowProgress, (void *) ((long)notifyFlags));
    }
#endif

    qfmt = rpmExpand("%{?_query_all_fmt}\n", NULL);
    for (arg = argv; *arg; arg++) {
	rpmdbMatchIterator mi;
	int matches = 0;
	int erasing = 1;

	/* Iterator count isn't reliable with labels, count manually... */
	mi = rpmtsInitIterator(ts, RPMDBI_LABEL, *arg, 0);
	while (rpmdbNextIterator(mi) != NULL) {
	    matches++;
	}
	rpmdbFreeIterator(mi);

	if (! matches) {
	    rpmlog(RPMLOG_ERR, _("package %s is not installed\n"), *arg);
	    numFailed++;
	} else {
	    Header h;	/* XXX iterator owns the reference */

	    if (matches > 1 && 
		!(ia->eraseInterfaceFlags & UNINSTALL_ALLMATCHES)) {
		rpmlog(RPMLOG_ERR, _("\"%s\" specifies multiple packages:\n"),
			*arg);
		numFailed++;
		erasing = 0;
	    }

	    mi = rpmtsInitIterator(ts, RPMDBI_LABEL, *arg, 0);
	    while ((h = rpmdbNextIterator(mi)) != NULL) {
		unsigned int recOffset;
		if (erasing && (recOffset = rpmdbGetIteratorOffset(mi))) {
		    (void) rpmtsAddEraseElement(ts, h, recOffset);
		    numPackages++;
		} else {
		    char *nevra = headerFormat(h, qfmt, NULL);
		    rpmlog(RPMLOG_NOTICE, "  %s", nevra);
		    free(nevra);
		}
	    }
	    mi = rpmdbFreeIterator(mi);
	}
    }
    free(qfmt);

    if (numFailed) goto exit;

    if (!(ia->eraseInterfaceFlags & UNINSTALL_NODEPS)) {

	if (rpmtsCheck(ts)) {
	    numFailed = numPackages;
	    stopUninstall = 1;
	}

	ps = rpmtsProblems(ts);
	if (!stopUninstall && rpmpsNumProblems(ps) > 0) {
	    rpmlog(RPMLOG_ERR, _("Failed dependencies:\n"));
	    rpmpsPrint(NULL, ps);
	    numFailed += numPackages;
	    stopUninstall = 1;
	}
	ps = rpmpsFree(ps);
    }

#if 0
    if (!stopUninstall && !(ia->installInterfaceFlags & INSTALL_NOORDER)) {
	if (rpmtsOrder(ts)) {
	    numFailed += numPackages;
	    stopUninstall = 1;
	}
    }
#endif

    if (numPackages && !stopUninstall) {
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | RPMTRANS_FLAG_REVERSE));

	/* Drop added/available package indices and dependency sets. */
	rpmtsClean(ts);

	numPackages = rpmtsRun(ts, NULL, ia->probFilter & (RPMPROB_FILTER_DISKSPACE|RPMPROB_FILTER_DISKNODES));
	ps = rpmtsProblems(ts);
	if (rpmpsNumProblems(ps) > 0)
	    rpmpsPrint(NULL, ps);
	numFailed += numPackages;
	stopUninstall = 1;
	ps = rpmpsFree(ps);
    }

exit:
    rpmtsEmpty(ts);

    return numFailed;
}

int rpmInstallSource(rpmts ts, const char * arg,
		char ** specFilePtr, char ** cookie)
{
    FD_t fd;
    int rc;


    fd = Fopen(arg, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rpmlog(RPMLOG_ERR, _("cannot open %s: %s\n"), arg, Fstrerror(fd));
	if (fd != NULL) (void) Fclose(fd);
	return 1;
    }

    if (rpmIsVerbose())
	fprintf(stdout, _("Installing %s\n"), arg);

    {
	rpmVSFlags ovsflags =
		rpmtsSetVSFlags(ts, (rpmtsVSFlags(ts) | RPMVSF_NEEDPAYLOAD));
	rpmRC rpmrc = rpmInstallSourcePackage(ts, fd, specFilePtr, cookie);
	rc = (rpmrc == RPMRC_OK ? 0 : 1);
	ovsflags = rpmtsSetVSFlags(ts, ovsflags);
    }
    if (rc != 0) {
	rpmlog(RPMLOG_ERR, _("%s cannot be installed\n"), arg);
	if (specFilePtr && *specFilePtr)
	    *specFilePtr = _free(*specFilePtr);
	if (cookie && *cookie)
	    *cookie = _free(*cookie);
    }

    (void) Fclose(fd);

    return rc;
}

