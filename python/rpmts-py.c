/** \ingroup py_c
 * \file python/rpmts-py.c
 */

#include "system.h"

#include <rpm/rpmlib.h>	/* rpmReadPackageFile, headerCheck */
#include <rpm/rpmtag.h>
#include <rpm/rpmpgp.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmbuild.h>

#include "header-py.h"
#include "rpmds-py.h"	/* XXX for rpmdsNew */
#include "rpmfi-py.h"	/* XXX for rpmfiNew */
#include "rpmmi-py.h"
#include "rpmps-py.h"
#include "rpmte-py.h"
#include "spec-py.h"

#include "rpmts-py.h"

#include "debug.h"

extern int _rpmts_debug;


/** \ingroup python
 * \name Class: Rpmts
 * \class Rpmts
 * \brief A python rpm.ts object represents an RPM transaction set.
 *
 * The transaction set is the workhorse of RPM.  It performs the
 * installation and upgrade of packages.  The rpm.ts object is
 * instantiated by the TransactionSet function in the rpm module.
 *
 * The TransactionSet function takes two optional arguments. The first
 * argument is the root path. The second is the verify signature disable flags,
 * a set of the following bits:
 *
 * -    rpm.RPMVSF_NOHDRCHK	if set, don't check rpmdb headers
 * -    rpm.RPMVSF_NEEDPAYLOAD	if not set, check header+payload (if possible)
 * -	rpm.RPMVSF_NOSHA1HEADER	if set, don't check header SHA1 digest
 * -	rpm.RPMVSF_NODSAHEADER	if set, don't check header DSA signature
 * -	rpm.RPMVSF_NOMD5	if set, don't check header+payload MD5 digest
 * -	rpm.RPMVSF_NODSA	if set, don't check header+payload DSA signature
 * -	rpm.RPMVSF_NORSA	if set, don't check header+payload RSA signature
 *
 * For convenience, there are the following masks:
 * -    rpm._RPMVSF_NODIGESTS		if set, don't check digest(s).
 * -    rpm._RPMVSF_NOSIGNATURES	if set, don't check signature(s).
 *
 * A rpm.ts object has the following methods:
 *
 * - addInstall(hdr,data,mode)  Add an install element to a transaction set.
 * @param hdr	the header to be added
 * @param data	user data that will be passed to the transaction callback
 *		during transaction execution
 * @param mode 	optional argument that specifies if this package should
 *		be installed ('i'), upgraded ('u').
 *
 * - addErase(name) Add an erase element to a transaction set.
 * @param name	the package name to be erased
 *
 * - check()	Perform a dependency check on the transaction set. After
 *		headers have been added to a transaction set, a dependency
 *		check can be performed to make sure that all package
 *		dependencies are satisfied.
 * @return	None If there are no unresolved dependencies
 *		Otherwise a list of complex tuples is returned, one tuple per
 *		unresolved dependency, with
 * The format of the dependency tuple is:
 *     ((packageName, packageVersion, packageRelease),
 *      (reqName, reqVersion),
 *      needsFlags,
 *      suggestedPackage,
 *      sense)
 *     packageName, packageVersion, packageRelease are the name,
 *     version, and release of the package that has the unresolved
 *     dependency or conflict.
 *     The reqName and reqVersion are the name and version of the
 *     requirement or conflict.
 *     The needsFlags is a bitfield that describes the versioned
 *     nature of a requirement or conflict.  The constants
 *     rpm.RPMSENSE_LESS, rpm.RPMSENSE_GREATER, and
 *     rpm.RPMSENSE_EQUAL can be logical ANDed with the needsFlags
 *     to get versioned dependency information.
 *     suggestedPackage is a tuple if the dependency check was aware
 *     of a package that solves this dependency problem when the
 *     dependency check was run.  Packages that are added to the
 *     transaction set as "available" are examined during the
 *     dependency check as possible dependency solvers. The tuple
 *     contains two values, (header, suggestedName).  These are set to
 *     the header of the suggested package and its name, respectively.
 *     If there is no known package to solve the dependency problem,
 *     suggestedPackage is None.
 *     The constants rpm.RPMDEP_SENSE_CONFLICTS and
 *     rpm.RPMDEP_SENSE_REQUIRES are set to show a dependency as a
 *     requirement or a conflict.
 *
 * - ts.order()	Do a topological sort of added element relations.
 * @return	None
 *
 * - ts.setFlags(transFlags) Set transaction set flags.
 * @param transFlags - bit(s) to controll transaction operations. The
 *		following values can be logically OR'ed together:
 *	- rpm.RPMTRANS_FLAG_TEST - test mode, do not modify the RPM
 *		database, change any files, or run any package scripts
 *	- rpm.RPMTRANS_FLAG_BUILD_PROBS - only build a list of
 *		problems encountered when attempting to run this transaction
 *		set
 *	- rpm.RPMTRANS_FLAG_NOSCRIPTS - do not execute package scripts
 *	- rpm.RPMTRANS_FLAG_JUSTDB - only make changes to the rpm
 *		database, do not modify files.
 *	- rpm.RPMTRANS_FLAG_NOTRIGGERS - do not run trigger scripts
 *	- rpm.RPMTRANS_FLAG_NODOCS - do not install files marked as %doc
 *	- rpm.RPMTRANS_FLAG_ALLFILES - create all files, even if a
 *		file is marked %config(missingok) and an upgrade is
 *		being performed.
 *	- rpm.RPMTRANS_FLAG_KEEPOBSOLETE - do not remove obsoleted
 *		packages.
 * @return	previous transFlags
 *
 * - ts.setProbFilter(ignoreSet) Set transaction set problem filter.
 * @param problemSetFilter - control bit(s) to ignore classes of problems,
 *		a logical or of one or more of the following bit(s):
 *	- rpm.RPMPROB_FILTER_IGNOREOS -
 *	- rpm.RPMPROB_FILTER_IGNOREARCH -
 *	- rpm.RPMPROB_FILTER_REPLACEPKG -
 *	- rpm.RPMPROB_FILTER_FORCERELOCATE -
 *	- rpm.RPMPROB_FILTER_REPLACENEWFILES -
 *	- rpm.RPMPROB_FILTER_REPLACEOLDFILES -
 *	- rpm.RPMPROB_FILTER_OLDPACKAGE -
 *	- rpm.RPMPROB_FILTER_DISKSPACE -
 * @return	previous ignoreSet
 *
 * - ts.run(callback,data) Attempt to execute a transaction set.
 *	After the transaction set has been populated with install/upgrade or
 *	erase actions, the transaction set can be executed by invoking
 *	the ts.run() method.
 */

/** \ingroup py_c
 */
struct rpmtsCallbackType_s {
    PyObject * cb;
    PyObject * data;
    rpmtsObject * tso;
    PyThreadState *_save;
};

/** \ingroup py_c
 */
static PyObject *
rpmts_Debug(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    char * kwlist[] = {"debugLevel", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:Debug", kwlist,
    	    &_rpmts_debug))
	return NULL;

if (_rpmts_debug < 0)
fprintf(stderr, "*** rpmts_Debug(%p) ts %p\n", s, s->ts);

    Py_INCREF(Py_None);
    return Py_None;
}

static void die(PyObject *cb)
{
    char *pyfn = NULL;
    PyObject *r;

    if (PyErr_Occurred()) {
	PyErr_Print();
    }
    if ((r = PyObject_Repr(cb)) != NULL) { 
	pyfn = PyString_AsString(r);
    }
    fprintf(stderr, _("error: python callback %s failed, aborting!\n"), 
	    	      pyfn ? pyfn : "???");
    rpmdbCheckTerminate(1);
    exit(EXIT_FAILURE);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_AddInstall(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    hdrObject * h;
    PyObject * key;
    char * how = "u";	/* XXX default to upgrade element if missing */
    int isUpgrade = 0;
    char * kwlist[] = {"header", "key", "how", NULL};
    int rc = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!O|s:AddInstall", kwlist,
    	    &hdr_Type, &h, &key, &how))
	return NULL;

    {	PyObject * hObj = (PyObject *) h;
	if (hObj->ob_type != &hdr_Type) {
	    PyErr_SetString(PyExc_TypeError, "bad type for header argument");
	    return NULL;
	}
    }

if (_rpmts_debug < 0 || (_rpmts_debug > 0))
fprintf(stderr, "*** rpmts_AddInstall(%p,%p,%p,%s) ts %p\n", s, h, key, how, s->ts);

    if (how && strcmp(how, "a") && strcmp(how, "u") && strcmp(how, "i")) {
	PyErr_SetString(PyExc_TypeError, "how argument must be \"u\" or \"i\"");
	return NULL;
    } else if (how && !strcmp(how, "u"))
    	isUpgrade = 1;

    rc = rpmtsAddInstallElement(s->ts, hdrGetHeader(h), key, isUpgrade, NULL);
    if (rc) {
	PyErr_SetString(pyrpmError, "adding package to transaction failed");
	return NULL;
    }
	

    /* This should increment the usage count for me */
    if (key)
	PyList_Append(s->keyList, key);

    Py_INCREF(Py_None);
    return Py_None;
}

/** \ingroup py_c
 * @todo Permit finer control (i.e. not just --allmatches) of deleted elments.
 */
static PyObject *
rpmts_AddErase(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject * o;
    int count;
    rpmdbMatchIterator mi;
    char * kwlist[] = {"name", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_AddErase(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:AddErase", kwlist, &o))
        return NULL;

    if (PyString_Check(o)) {
	char * name = PyString_AsString(o);

	mi = rpmtsInitIterator(s->ts, RPMDBI_LABEL, name, 0);
	count = rpmdbGetIteratorCount(mi);
	if (count <= 0) {
	    mi = rpmdbFreeIterator(mi);
	    PyErr_SetString(pyrpmError, "package not installed");
	    return NULL;
	} else { /* XXX: Note that we automatically choose to remove all matches */
	    Header h;
	    while ((h = rpmdbNextIterator(mi)) != NULL) {
		unsigned int recOffset = rpmdbGetIteratorOffset(mi);
		if (recOffset)
		    rpmtsAddEraseElement(s->ts, h, recOffset);
	    }
	}
	mi = rpmdbFreeIterator(mi);
    } else
    if (PyInt_Check(o)) {
	uint32_t instance = PyInt_AsLong(o);

	mi = rpmtsInitIterator(s->ts, RPMDBI_PACKAGES, &instance, sizeof(instance));
	if (instance == 0 || mi == NULL) {
	    mi = rpmdbFreeIterator(mi);
	    PyErr_SetString(pyrpmError, "package not installed");
	    return NULL;
	} else {
	    Header h;
	    while ((h = rpmdbNextIterator(mi)) != NULL) {
		uint32_t recOffset = rpmdbGetIteratorOffset(mi);
		if (recOffset)
		    rpmtsAddEraseElement(s->ts, h, recOffset);
		break;
	    }
	}
	mi = rpmdbFreeIterator(mi);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

/** \ingroup py_c
 */
static int
rpmts_SolveCallback(rpmts ts, rpmds ds, const void * data)
{
    struct rpmtsCallbackType_s * cbInfo = (struct rpmtsCallbackType_s *) data;
    PyObject * args, * result;
    int res = 1;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_SolveCallback(%p,%p,%p) \"%s\"\n", ts, ds, data, rpmdsDNEVR(ds));

    if (cbInfo->tso == NULL) return res;
    if (cbInfo->cb == Py_None) return res;

    PyEval_RestoreThread(cbInfo->_save);

    args = Py_BuildValue("(Oissi)", cbInfo->tso,
		rpmdsTagN(ds), rpmdsN(ds), rpmdsEVR(ds), rpmdsFlags(ds));
    result = PyEval_CallObject(cbInfo->cb, args);
    Py_DECREF(args);

    if (!result) {
	die(cbInfo->cb);
    } else {
	if (PyInt_Check(result))
	    res = PyInt_AsLong(result);
	Py_DECREF(result);
    }

    cbInfo->_save = PyEval_SaveThread();

    return res;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_Check(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    rpmps ps;
    rpmProblem p;
    PyObject * list, * cf;
    struct rpmtsCallbackType_s cbInfo;
    int i;
    int xx;
    char * kwlist[] = {"callback", NULL};

    memset(&cbInfo, 0, sizeof(cbInfo));
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:Check", kwlist,
	    &cbInfo.cb))
	return NULL;

    if (cbInfo.cb != NULL) {
	if (!PyCallable_Check(cbInfo.cb)) {
	    PyErr_SetString(PyExc_TypeError, "expected a callable");
	    return NULL;
	}
	xx = rpmtsSetSolveCallback(s->ts, rpmts_SolveCallback, (void *)&cbInfo);
    }

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Check(%p) ts %p cb %p\n", s, s->ts, cbInfo.cb);

    cbInfo.tso = s;
    cbInfo._save = PyEval_SaveThread();

    xx = rpmtsCheck(s->ts);
    ps = rpmtsProblems(s->ts);

    PyEval_RestoreThread(cbInfo._save);

    if (ps != NULL) {
	list = PyList_New(0);
	rpmpsi psi = rpmpsInitIterator(ps);

	/* XXX TODO: rpmlib >= 4.0.3 can return multiple suggested keys. */
	while ((i = rpmpsNextIterator(psi)) >= 0) {
	    char * altNEVR, * needsName;
	    char * byName, * byVersion, * byRelease, *byArch;
	    char * needsOP, * needsVersion;
	    rpmsenseFlags needsFlags, sense;
	    fnpyKey key;

	    p = rpmpsGetProblem(psi);

	    byName = xstrdup(rpmProblemGetPkgNEVR(p));
	    if ((byArch= strrchr(byName, '.')) != NULL)
		*byArch++ = '\0';
	    if ((byRelease = strrchr(byName, '-')) != NULL)
		*byRelease++ = '\0';
	    if ((byVersion = strrchr(byName, '-')) != NULL)
		*byVersion++ = '\0';

	    key = rpmProblemGetKey(p);

	    altNEVR = needsName = xstrdup(rpmProblemGetAltNEVR(p));
	    if (needsName[1] == ' ') {
		sense = (needsName[0] == 'C')
			? RPMDEP_SENSE_CONFLICTS : RPMDEP_SENSE_REQUIRES;
		needsName += 2;
	    } else
		sense = RPMDEP_SENSE_REQUIRES;
	    if ((needsVersion = strrchr(needsName, ' ')) != NULL)
		*needsVersion++ = '\0';

	    needsFlags = 0;
	    if ((needsOP = strrchr(needsName, ' ')) != NULL) {
		for (*needsOP++ = '\0'; *needsOP != '\0'; needsOP++) {
		    if (*needsOP == '<')	needsFlags |= RPMSENSE_LESS;
		    else if (*needsOP == '>')	needsFlags |= RPMSENSE_GREATER;
		    else if (*needsOP == '=')	needsFlags |= RPMSENSE_EQUAL;
		}
	    }

	    cf = Py_BuildValue("((sss)(ss)iOi)", byName, byVersion, byRelease,
			       needsName, needsVersion, needsFlags,
			       (key != NULL ? key : Py_None),
			       sense);
	    PyList_Append(list, (PyObject *) cf);
	    Py_DECREF(cf);
	    free(byName);
	    free(altNEVR);
	}

	psi = rpmpsFreeIterator(psi);
	ps = rpmpsFree(ps);

	return list;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_Order(rpmtsObject * s)
{
    int rc;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Order(%p) ts %p\n", s, s->ts);

    Py_BEGIN_ALLOW_THREADS
    rc = rpmtsOrder(s->ts);
    Py_END_ALLOW_THREADS

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_Clean(rpmtsObject * s)
{
if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Clean(%p) ts %p\n", s, s->ts);

    rpmtsClean(s->ts);

    Py_INCREF(Py_None);
    return Py_None;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_OpenDB(rpmtsObject * s)
{
    int dbmode;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_OpenDB(%p) ts %p\n", s, s->ts);

    dbmode = rpmtsGetDBMode(s->ts);
    if (dbmode == -1)
	dbmode = O_RDONLY;

    return Py_BuildValue("i", rpmtsOpenDB(s->ts, dbmode));
}

/** \ingroup py_c
 */
static PyObject *
rpmts_CloseDB(rpmtsObject * s)
{
    int rc;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_CloseDB(%p) ts %p\n", s, s->ts);

    rc = rpmtsCloseDB(s->ts);
    rpmtsSetDBMode(s->ts, -1);	/* XXX disable lazy opens */

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_InitDB(rpmtsObject * s)
{
    int rc;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_InitDB(%p) ts %p\n", s, s->ts);

    rc = rpmtsInitDB(s->ts, O_RDONLY);
    if (rc == 0)
	rc = rpmtsCloseDB(s->ts);

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_RebuildDB(rpmtsObject * s)
{
    int rc;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_RebuildDB(%p) ts %p\n", s, s->ts);

    Py_BEGIN_ALLOW_THREADS
    rc = rpmtsRebuildDB(s->ts);
    Py_END_ALLOW_THREADS

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_VerifyDB(rpmtsObject * s)
{
    int rc;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_VerifyDB(%p) ts %p\n", s, s->ts);

    Py_BEGIN_ALLOW_THREADS
    rc = rpmtsVerifyDB(s->ts);
    Py_END_ALLOW_THREADS

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_HdrFromFdno(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject * result = NULL;
    Header h;
    FD_t fd;
    int fdno;
    rpmRC rpmrc;
    char * kwlist[] = {"fd", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:HdrFromFdno", kwlist,
	    &fdno))
    	return NULL;

    fd = fdDup(fdno);
    rpmrc = rpmReadPackageFile(s->ts, fd, "rpmts_HdrFromFdno", &h);
    Fclose(fd);

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_HdrFromFdno(%p) ts %p rc %d\n", s, s->ts, rpmrc);

    switch (rpmrc) {
    case RPMRC_OK:
	if (h)
	    result = Py_BuildValue("N", hdr_Wrap(h));
	h = headerFree(h);	/* XXX ref held by result */
	break;

    case RPMRC_NOKEY:
	PyErr_SetString(pyrpmError, "public key not available");
	break;

    case RPMRC_NOTTRUSTED:
	PyErr_SetString(pyrpmError, "public key not trusted");
	break;

    case RPMRC_NOTFOUND:
    case RPMRC_FAIL:
    default:
	PyErr_SetString(pyrpmError, "error reading package header");
	break;
    }

    return result;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_HdrCheck(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject * blob;
    PyObject * result = NULL;
    char * msg = NULL;
    const void * uh;
    int uc;
    rpmRC rpmrc;
    char * kwlist[] = {"headers", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_HdrCheck(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:HdrCheck", kwlist, &blob))
    	return NULL;

    if (blob == Py_None) {
	Py_INCREF(Py_None);
	return Py_None;
    }
    if (!PyString_Check(blob)) {
	PyErr_SetString(pyrpmError, "hdrCheck takes a string of octets");
	return result;
    }
    uh = PyString_AsString(blob);
    uc = PyString_Size(blob);

    rpmrc = headerCheck(s->ts, uh, uc, &msg);

    switch (rpmrc) {
    case RPMRC_OK:
	Py_INCREF(Py_None);
	result = Py_None;
	break;

    case RPMRC_NOKEY:
	PyErr_SetString(pyrpmError, "public key not availaiable");
	break;

    case RPMRC_NOTTRUSTED:
	PyErr_SetString(pyrpmError, "public key not trusted");
	break;

    case RPMRC_FAIL:
    default:
	PyErr_SetString(pyrpmError, msg);
	break;
    }
    msg = _free(msg);

    return result;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_SetVSFlags(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    rpmVSFlags vsflags;
    char * kwlist[] = {"flags", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_SetVSFlags(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:SetVSFlags", kwlist,
	    &vsflags))
    	return NULL;

    /* XXX FIXME: value check on vsflags, or build pure python object 
     * for it, and require an object of that type */

    return Py_BuildValue("i", rpmtsSetVSFlags(s->ts, vsflags));
}

/** \ingroup py_c
 */
static PyObject *
rpmts_GetVSFlags(rpmtsObject * s)
{
    return Py_BuildValue("i", rpmtsVSFlags(s->ts));
}

/** \ingroup py_c
 */
static PyObject *
rpmts_SetColor(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    rpm_color_t tscolor;
    char * kwlist[] = {"color", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_SetColor(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:Color", kwlist, &tscolor))
    	return NULL;

    /* XXX FIXME: value check on tscolor, or build pure python object
     * for it, and require an object of that type */

    return Py_BuildValue("i", rpmtsSetColor(s->ts, tscolor));
}

/** \ingroup py_c
 */
static PyObject *
rpmts_PgpPrtPkts(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject * blob;
    unsigned char * pkt;
    unsigned int pktlen;
    int rc;
    char * kwlist[] = {"octets", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_PgpPrtPkts(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:PgpPrtPkts", kwlist, &blob))
    	return NULL;

    if (blob == Py_None) {
	Py_INCREF(Py_None);
	return Py_None;
    }
    if (!PyString_Check(blob)) {
	PyErr_SetString(pyrpmError, "pgpPrtPkts takes a string of octets");
	return NULL;
    }
    pkt = (unsigned char *)PyString_AsString(blob);
    pktlen = PyString_Size(blob);

    rc = pgpPrtPkts(pkt, pktlen, NULL, 1);

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_PgpImportPubkey(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject * blob;
    unsigned char * pkt;
    unsigned int pktlen;
    int rc;
    char * kwlist[] = {"pubkey", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_PgpImportPubkey(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O:PgpImportPubkey",
    	    kwlist, &blob))
	return NULL;

    if (blob == Py_None) {
	Py_INCREF(Py_None);
	return Py_None;
    }
    if (!PyString_Check(blob)) {
	PyErr_SetString(pyrpmError, "PgpImportPubkey takes a string of octets");
	return NULL;
    }
    pkt = (unsigned char *)PyString_AsString(blob);
    pktlen = PyString_Size(blob);

    rc = rpmtsImportPubkey(s->ts, pkt, pktlen);

    return Py_BuildValue("i", rc);
}

/** \ingroup py_c
 */
static PyObject *
rpmts_GetKeys(rpmtsObject * s)
{
    const void **data = NULL;
    int num, i;
    PyObject *tuple;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_GetKeys(%p) ts %p\n", s, s->ts);

    rpmtsGetKeys(s->ts, &data, &num);
    if (data == NULL || num <= 0) {
	data = _free(data);
	Py_INCREF(Py_None);
	return Py_None;
    }

    tuple = PyTuple_New(num);

    for (i = 0; i < num; i++) {
	PyObject *obj;
	obj = (data[i] ? (PyObject *) data[i] : Py_None);
	Py_INCREF(obj);
	PyTuple_SetItem(tuple, i, obj);
    }

    data = _free(data);

    return tuple;
}

/** \ingroup py_c
 */
static void *
rpmtsCallback(const void * hd, const rpmCallbackType what,
		         const rpm_loff_t amount, const rpm_loff_t total,
	                 const void * pkgKey, rpmCallbackData data)
{
    Header h = (Header) hd;
    struct rpmtsCallbackType_s * cbInfo = data;
    PyObject * pkgObj = (PyObject *) pkgKey;
    PyObject * args, * result;
    static FD_t fd;

    if (cbInfo->cb == Py_None) return NULL;

    /* Synthesize a python object for callback (if necessary). */
    if (pkgObj == NULL) {
	if (h) {
	    const char * n = NULL;
	    (void) headerNVR(h, &n, NULL, NULL);
	    pkgObj = Py_BuildValue("s", n);
	} else {
	    pkgObj = Py_None;
	    Py_INCREF(pkgObj);
	}
    } else
	Py_INCREF(pkgObj);

    PyEval_RestoreThread(cbInfo->_save);

    args = Py_BuildValue("(iLLOO)", what, amount, total, pkgObj, cbInfo->data);
    result = PyEval_CallObject(cbInfo->cb, args);
    Py_DECREF(args);
    Py_DECREF(pkgObj);

    if (!result) {
	die(cbInfo->cb);
    }

    if (what == RPMCALLBACK_INST_OPEN_FILE) {
	int fdno;

        if (!PyArg_Parse(result, "i", &fdno)) {
	    die(cbInfo->cb);
	}
	Py_DECREF(result);
	cbInfo->_save = PyEval_SaveThread();

	fd = fdDup(fdno);
if (_rpmts_debug)
fprintf(stderr, "\t%p = fdDup(%d)\n", fd, fdno);

	fcntl(Fileno(fd), F_SETFD, FD_CLOEXEC);

	return fd;
    } else
    if (what == RPMCALLBACK_INST_CLOSE_FILE) {
if (_rpmts_debug)
fprintf(stderr, "\tFclose(%p)\n", fd);
	Fclose (fd);
    } else {
if (_rpmts_debug)
fprintf(stderr, "\t%llu:%llu key %p\n", (long long) amount, (long long) total, pkgKey);
    }

    Py_DECREF(result);
    cbInfo->_save = PyEval_SaveThread();

    return NULL;
}

/** \ingroup py_c
 */
static PyObject *
rpmts_SetFlags(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    rpmtransFlags transFlags = 0;
    char * kwlist[] = {"flags", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:SetFlags", kwlist,
	    &transFlags))
	return NULL;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_SetFlags(%p) ts %p transFlags %x\n", s, s->ts, transFlags);

    /* XXX FIXME: value check on flags, or build pure python object 
     * for it, and require an object of that type */

    return Py_BuildValue("i", rpmtsSetFlags(s->ts, transFlags));
}

/** \ingroup py_c
 */
static PyObject *
rpmts_SetProbFilter(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    rpmprobFilterFlags ignoreSet = 0;
    rpmprobFilterFlags oignoreSet;
    char * kwlist[] = {"ignoreSet", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:ProbFilter", kwlist,
	    &ignoreSet))
	return NULL;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_SetProbFilter(%p) ts %p ignoreSet %x\n", s, s->ts, ignoreSet);

    oignoreSet = s->ignoreSet;
    s->ignoreSet = ignoreSet;

    return Py_BuildValue("i", oignoreSet);
}

/** \ingroup py_c
 */
static rpmpsObject *
rpmts_Problems(rpmtsObject * s)
{

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Problems(%p) ts %p\n", s, s->ts);

    return rpmps_Wrap( rpmtsProblems(s->ts) );
}

/** \ingroup py_c
 */
static PyObject *
rpmts_Run(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    int rc;
    PyObject * list;
    rpmps ps;
    rpmpsi psi;
    struct rpmtsCallbackType_s cbInfo;
    char * kwlist[] = {"callback", "data", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO:Run", kwlist,
	    &cbInfo.cb, &cbInfo.data))
	return NULL;

    cbInfo.tso = s;
    cbInfo._save = PyEval_SaveThread();

    if (cbInfo.cb != NULL) {
	if (!PyCallable_Check(cbInfo.cb)) {
	    PyErr_SetString(PyExc_TypeError, "expected a callable");
	    return NULL;
	}
	(void) rpmtsSetNotifyCallback(s->ts, rpmtsCallback, (void *) &cbInfo);
    }

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Run(%p) ts %p ignore %x\n", s, s->ts, s->ignoreSet);

    rc = rpmtsRun(s->ts, NULL, s->ignoreSet);
    ps = rpmtsProblems(s->ts);

    if (cbInfo.cb)
	(void) rpmtsSetNotifyCallback(s->ts, NULL, NULL);

    PyEval_RestoreThread(cbInfo._save);

    if (rc < 0) {
	list = PyList_New(0);
	return list;
    } else if (!rc) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    list = PyList_New(0);
    psi = rpmpsInitIterator(ps);
    while (rpmpsNextIterator(psi) >= 0) {
	rpmProblem p = rpmpsGetProblem(psi);
	char * ps = rpmProblemString(p);
	PyObject * prob = Py_BuildValue("s(isN)", ps,
			     rpmProblemGetType(p),
			     rpmProblemGetStr(p),
			     PyLong_FromLongLong(rpmProblemGetDiskNeed(p)));
	PyList_Append(list, prob);
	free(ps);
	Py_DECREF(prob);
    }

    psi = rpmpsFreeIterator(psi);
    ps = rpmpsFree(ps);

    return list;
}

#if Py_TPFLAGS_HAVE_ITER
static PyObject *
rpmts_iter(rpmtsObject * s)
{
if (_rpmts_debug)
fprintf(stderr, "*** rpmts_iter(%p) ts %p\n", s, s->ts);

    Py_INCREF(s);
    return (PyObject *)s;
}
#endif

/**
 * @todo Add TR_ADDED filter to iterator.
 */
static PyObject *
rpmts_iternext(rpmtsObject * s)
{
    PyObject * result = NULL;
    rpmte te;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_iternext(%p) ts %p tsi %p %d\n", s, s->ts, s->tsi, s->tsiFilter);

    /* Reset iterator on 1st entry. */
    if (s->tsi == NULL) {
	s->tsi = rpmtsiInit(s->ts);
	if (s->tsi == NULL)
	    return NULL;
	s->tsiFilter = 0;
    }

    te = rpmtsiNext(s->tsi, s->tsiFilter);
    if (te != NULL) {
	result = (PyObject *) rpmte_Wrap(te);
    } else {
	s->tsi = rpmtsiFree(s->tsi);
	s->tsiFilter = 0;
    }

    return result;
}

/**
 * @todo Add TR_ADDED filter to iterator.
 */
static PyObject *
rpmts_Next(rpmtsObject * s)
{
    PyObject * result;

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Next(%p) ts %p\n", s, s->ts);

    result = rpmts_iternext(s);

    if (result == NULL) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    return result;
}

/**
 */
static specObject *
spec_Parse(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    const char * specfile;
    rpmSpec spec;
    char * buildRoot = NULL;
    int recursing = 0;
    char * passPhrase = "";
    char *cookie = NULL;
    int anyarch = 1;
    int force = 1;
    char * kwlist[] = {"specfile", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s:Parse", kwlist, &specfile))
	return NULL;

    if (parseSpec(s->ts, specfile,"/", buildRoot,recursing, passPhrase,
             cookie, anyarch, force)!=0) {
             PyErr_SetString(pyrpmError, "can't parse specfile\n");
                     return NULL;
   }

    spec = rpmtsSpec(s->ts);
    return spec_Wrap(spec);
}

/**
 */
static rpmmiObject *
rpmts_Match(rpmtsObject * s, PyObject * args, PyObject * kwds)
{
    PyObject *TagN = NULL;
    PyObject *Key = NULL;
    char *key = NULL;
/* XXX lkey *must* be a 32 bit integer, int "works" on all known platforms. */
    int lkey = 0;
    int len = 0;
    rpmTag tag = RPMDBI_PACKAGES;
    char * kwlist[] = {"tagNumber", "key", NULL};

if (_rpmts_debug)
fprintf(stderr, "*** rpmts_Match(%p) ts %p\n", s, s->ts);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO:Match", kwlist,
	    &TagN, &Key))
	return NULL;

    if (TagN && (tag = tagNumFromPyObject (TagN)) == -1) {
	PyErr_SetString(PyExc_TypeError, "unknown tag type");
	return NULL;
    }

    if (Key) {
	if (PyString_Check(Key) || PyUnicode_Check(Key)) {
	    key = PyString_AsString(Key);
	    len = PyString_Size(Key);
	} else if (PyInt_Check(Key)) {
	    lkey = PyInt_AsLong(Key);
	    key = (char *)&lkey;
	    len = sizeof(lkey);
	} else {
	    PyErr_SetString(PyExc_TypeError, "unknown key type");
	    return NULL;
	}
	/* One of the conversions above failed, exception is set already */
	if (PyErr_Occurred()) {
	    return NULL;
	}
    }

    /* XXX If not already opened, open the database O_RDONLY now. */
    /* XXX FIXME: lazy default rdonly open also done by rpmtsInitIterator(). */
    if (rpmtsGetRdb(s->ts) == NULL) {
	int rc = rpmtsOpenDB(s->ts, O_RDONLY);
	if (rc || rpmtsGetRdb(s->ts) == NULL) {
	    PyErr_SetString(PyExc_TypeError, "rpmdb open failed");
	    return NULL;
	}
    }

    return rpmmi_Wrap( rpmtsInitIterator(s->ts, tag, key, len), (PyObject*)s);
}

/** \ingroup py_c
 */
static struct PyMethodDef rpmts_methods[] = {
 {"Debug",	(PyCFunction)rpmts_Debug,	METH_VARARGS|METH_KEYWORDS,
        NULL},

 {"addInstall",	(PyCFunction) rpmts_AddInstall,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"addErase",	(PyCFunction) rpmts_AddErase,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"check",	(PyCFunction) rpmts_Check,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"order",	(PyCFunction) rpmts_Order,	METH_NOARGS,
	NULL },
 {"setFlags",	(PyCFunction) rpmts_SetFlags,	METH_VARARGS|METH_KEYWORDS,
"ts.setFlags(transFlags) -> previous transFlags\n\
- Set control bit(s) for executing ts.run().\n\
  Note: This method replaces the 1st argument to the old ts.run()\n" },
 {"setProbFilter",	(PyCFunction) rpmts_SetProbFilter,	METH_VARARGS|METH_KEYWORDS,
"ts.setProbFilter(ignoreSet) -> previous ignoreSet\n\
- Set control bit(s) for ignoring problems found by ts.run().\n\
  Note: This method replaces the 2nd argument to the old ts.run()\n" },
 {"problems",	(PyCFunction) rpmts_Problems,	METH_NOARGS,
"ts.problems() -> ps\n\
- Return current problem set.\n" },
 {"run",	(PyCFunction) rpmts_Run,	METH_VARARGS|METH_KEYWORDS,
"ts.run(callback, data) -> (problems)\n\
- Run a transaction set, returning list of problems found.\n\
  Note: The callback may not be None.\n" },
 {"clean",	(PyCFunction) rpmts_Clean,	METH_NOARGS,
	NULL },
 {"openDB",	(PyCFunction) rpmts_OpenDB,	METH_NOARGS,
"ts.openDB() -> None\n\
- Open the default transaction rpmdb.\n\
  Note: The transaction rpmdb is lazily opened, so ts.openDB() is seldom needed.\n" },
 {"closeDB",	(PyCFunction) rpmts_CloseDB,	METH_NOARGS,
"ts.closeDB() -> None\n\
- Close the default transaction rpmdb.\n\
  Note: ts.closeDB() disables lazy opens, and should hardly ever be used.\n" },
 {"initDB",	(PyCFunction) rpmts_InitDB,	METH_NOARGS,
"ts.initDB() -> None\n\
- Initialize the default transaction rpmdb.\n\
 Note: ts.initDB() is seldom needed anymore.\n" },
 {"rebuildDB",	(PyCFunction) rpmts_RebuildDB,	METH_NOARGS,
"ts.rebuildDB() -> None\n\
- Rebuild the default transaction rpmdb.\n" },
 {"verifyDB",	(PyCFunction) rpmts_VerifyDB,	METH_NOARGS,
"ts.verifyDB() -> None\n\
- Verify the default transaction rpmdb.\n" },
 {"hdrFromFdno",(PyCFunction) rpmts_HdrFromFdno,METH_VARARGS|METH_KEYWORDS,
"ts.hdrFromFdno(fdno) -> hdr\n\
- Read a package header from a file descriptor.\n" },
 {"hdrCheck",	(PyCFunction) rpmts_HdrCheck,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"setVSFlags",(PyCFunction) rpmts_SetVSFlags,	METH_VARARGS|METH_KEYWORDS,
"ts.setVSFlags(vsflags) -> ovsflags\n\
- Set signature verification flags. Values for vsflags are:\n\
    rpm.RPMVSF_NOHDRCHK      if set, don't check rpmdb headers\n\
    rpm.RPMVSF_NEEDPAYLOAD   if not set, check header+payload (if possible)\n\
    rpm.RPMVSF_NOSHA1HEADER  if set, don't check header SHA1 digest\n\
    rpm.RPMVSF_NODSAHEADER   if set, don't check header DSA signature\n\
    rpm.RPMVSF_NOMD5         if set, don't check header+payload MD5 digest\n\
    rpm.RPMVSF_NODSA         if set, don't check header+payload DSA signature\n\
    rpm.RPMVSF_NORSA         if set, don't check header+payload RSA signature\n\
    rpm._RPMVSF_NODIGESTS    if set, don't check digest(s)\n\
    rpm._RPMVSF_NOSIGNATURES if set, don't check signature(s)\n" },
 {"getVSFlags",(PyCFunction) rpmts_GetVSFlags,	METH_NOARGS,
"ts.getVSFlags() -> vsflags\n\
- Retrieve current signature verification flags from transaction\n" },
 {"setColor",(PyCFunction) rpmts_SetColor,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"pgpPrtPkts",	(PyCFunction) rpmts_PgpPrtPkts,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"pgpImportPubkey",	(PyCFunction) rpmts_PgpImportPubkey,	METH_VARARGS|METH_KEYWORDS,
	NULL },
 {"getKeys",	(PyCFunction) rpmts_GetKeys,	METH_NOARGS,
	NULL },
 {"parseSpec",	(PyCFunction) spec_Parse,	METH_VARARGS|METH_KEYWORDS,
"ts.parseSpec(\"/path/to/foo.spec\") -> spec\n\
- Parse a spec file.\n" },
 {"dbMatch",	(PyCFunction) rpmts_Match,	METH_VARARGS|METH_KEYWORDS,
"ts.dbMatch([TagN, [key, [len]]]) -> mi\n\
- Create a match iterator for the default transaction rpmdb.\n" },
 {"next",		(PyCFunction)rpmts_Next,	METH_NOARGS,
"ts.next() -> te\n\
- Retrieve next transaction set element.\n" },
    {NULL,		NULL}		/* sentinel */
};

/** \ingroup py_c
 */
static void rpmts_dealloc(rpmtsObject * s)
{

if (_rpmts_debug)
fprintf(stderr, "%p -- ts %p db %p\n", s, s->ts, rpmtsGetRdb(s->ts));
    s->ts = rpmtsFree(s->ts);

    if (s->scriptFd) Fclose(s->scriptFd);
    /* this will free the keyList, and decrement the ref count of all
       the items on the list as well :-) */
    Py_DECREF(s->keyList);
    PyObject_Del((PyObject *)s);
}

static PyObject * rpmts_getattro(PyObject * o, PyObject * n)
{
    return PyObject_GenericGetAttr(o, n);
}

/** \ingroup py_c
 */
static int rpmts_setattro(PyObject * o, PyObject * n, PyObject * v)
{
    rpmtsObject *s = (rpmtsObject *)o;
    char * name = PyString_AsString(n);
    int fdno;

    if (!strcmp(name, "scriptFd")) {
	if (!PyArg_Parse(v, "i", &fdno)) return 0;
	if (fdno < 0) {
	    PyErr_SetString(PyExc_TypeError, "bad file descriptor");
	    return -1;
	} else {
	    s->scriptFd = fdDup(fdno);
	    rpmtsSetScriptFd(s->ts, s->scriptFd);
	}
    } else {
	PyErr_SetString(PyExc_AttributeError, name);
	return -1;
    }

    return 0;
}

/** \ingroup py_c
 */
static int rpmts_init(rpmtsObject * s, PyObject *args, PyObject *kwds)
{
    /* nothing to do atm... */
    return 0;
}

/** \ingroup py_c
 */
static void rpmts_free(rpmtsObject * s)
{
if (_rpmts_debug)
fprintf(stderr, "%p -- ts %p db %p\n", s, s->ts, rpmtsGetRdb(s->ts));
    s->ts = rpmtsFree(s->ts);

    if (s->scriptFd)
	Fclose(s->scriptFd);

    /* this will free the keyList, and decrement the ref count of all
       the items on the list as well :-) */
    Py_DECREF(s->keyList);

    PyObject_Del((PyObject *)s);
}

/** \ingroup py_c
 */
static PyObject * rpmts_alloc(PyTypeObject * subtype, int nitems)
{
    PyObject * s = PyType_GenericAlloc(subtype, nitems);

if (_rpmts_debug < 0)
fprintf(stderr, "*** rpmts_alloc(%p,%d) ret %p\n", subtype, nitems, s);
    return s;
}

/** \ingroup py_c
 */
static PyObject * rpmts_new(PyTypeObject * subtype, PyObject *args, PyObject *kwds)
{
    rpmtsObject * s = (void *) PyObject_New(rpmtsObject, subtype);

    char * rootDir = "/";
    rpmVSFlags vsflags = rpmExpandNumeric("%{?__vsflags}");
    char * kwlist[] = {"rootdir", "vsflags", 0};

    if (_rpmts_debug < 0)
	fprintf(stderr, "*** rpmts_new(%p,%p,%p)\n", s, args, kwds);

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|si:rpmts_init", kwlist,
	    &rootDir, &vsflags))
	return NULL;

    s->ts = rpmtsCreate();
    /* XXX: Why is there no rpmts_SetRootDir() ? */
    (void) rpmtsSetRootDir(s->ts, rootDir);
    /* XXX: make this use common code with rpmts_SetVSFlags() to check the
     *      python objects */
    (void) rpmtsSetVSFlags(s->ts, vsflags);
    s->keyList = PyList_New(0);
    s->scriptFd = NULL;
    s->tsi = NULL;
    s->tsiFilter = 0;

    if (_rpmts_debug)
	fprintf(stderr, "%p ++ ts %p db %p\n", s, s->ts, rpmtsGetRdb(s->ts));

    return (PyObject *)s;
}

/**
 */
static char rpmts_doc[] =
"";

/** \ingroup py_c
 */
PyTypeObject rpmts_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,				/* ob_size */
	"rpm.ts",			/* tp_name */
	sizeof(rpmtsObject),		/* tp_size */
	0,				/* tp_itemsize */
	(destructor) rpmts_dealloc, 	/* tp_dealloc */
	0,				/* tp_print */
	(getattrfunc)0, 		/* tp_getattr */
	(setattrfunc)0,			/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	(getattrofunc) rpmts_getattro, 	/* tp_getattro */
	(setattrofunc) rpmts_setattro,	/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT, 		/* tp_flags */
	rpmts_doc,			/* tp_doc */
#if Py_TPFLAGS_HAVE_ITER
	0,				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	(getiterfunc) rpmts_iter,	/* tp_iter */
	(iternextfunc) rpmts_iternext,	/* tp_iternext */
	rpmts_methods,			/* tp_methods */
	0,				/* tp_members */
	0,				/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	(initproc) rpmts_init,		/* tp_init */
	(allocfunc) rpmts_alloc,	/* tp_alloc */
	(newfunc) rpmts_new,		/* tp_new */
	(freefunc) rpmts_free,		/* tp_free */
	0,				/* tp_is_gc */
#endif
};

/**
 */
PyObject *
rpmts_Create(PyObject * self, PyObject * args, PyObject * kwds)
{
    return PyObject_Call((PyObject *) &rpmts_Type, args, kwds);
}
