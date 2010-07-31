#ifndef H_RPMDS
#define H_RPMDS

/** \ingroup rpmdep rpmtrans
 * \file lib/rpmds.h
 * Structure(s) used for dependency tag sets.
 */

#include <time.h>

#include <rpm/rpmtypes.h>
#include <rpm/rpmps.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 */
extern int _rpmds_debug;

/**
 */
extern int _rpmds_nopromote;

/** \ingroup rpmds
 * Dependency Attributes.
 */
typedef	enum rpmsenseFlags_e {
    RPMSENSE_ANY	= 0,
    RPMSENSE_LESS	= (1 << 1),
    RPMSENSE_GREATER	= (1 << 2),
    RPMSENSE_EQUAL	= (1 << 3),
    RPMSENSE_PROVIDES	= (1 << 4), /* only used internally by builds */
    RPMSENSE_CONFLICTS	= (1 << 5), /* only used internally by builds */
    RPMSENSE_PREREQ	= (1 << 6), /* legacy prereq dependency */
    RPMSENSE_OBSOLETES	= (1 << 7), /* only used internally by builds */
    RPMSENSE_INTERP	= (1 << 8),	/*!< Interpreter used by scriptlet. */
    RPMSENSE_SCRIPT_PRE	= (1 << 9),	/*!< %pre dependency. */
    RPMSENSE_SCRIPT_POST = (1 << 10),	/*!< %post dependency. */
    RPMSENSE_SCRIPT_PREUN = (1 << 11),	/*!< %preun dependency. */
    RPMSENSE_SCRIPT_POSTUN = (1 << 12), /*!< %postun dependency. */
    RPMSENSE_SCRIPT_VERIFY = (1 << 13),	/*!< %verify dependency. */
    RPMSENSE_FIND_REQUIRES = (1 << 14), /*!< find-requires generated dependency. */
    RPMSENSE_FIND_PROVIDES = (1 << 15), /*!< find-provides generated dependency. */

    RPMSENSE_TRIGGERIN	= (1 << 16),	/*!< %triggerin dependency. */
    RPMSENSE_TRIGGERUN	= (1 << 17),	/*!< %triggerun dependency. */
    RPMSENSE_TRIGGERPOSTUN = (1 << 18),	/*!< %triggerpostun dependency. */
    RPMSENSE_MISSINGOK	= (1 << 19),	/*!< suggests/enhances hint. */
    RPMSENSE_SCRIPT_PREP = (1 << 20),	/*!< %prep build dependency. */
    RPMSENSE_SCRIPT_BUILD = (1 << 21),	/*!< %build build dependency. */
    RPMSENSE_SCRIPT_INSTALL = (1 << 22),/*!< %install build dependency. */
    RPMSENSE_SCRIPT_CLEAN = (1 << 23),	/*!< %clean build dependency. */
    RPMSENSE_RPMLIB = (1 << 24),	/*!< rpmlib(feature) dependency. */
    RPMSENSE_TRIGGERPREIN = (1 << 25),	/*!< %triggerprein dependency. */
    RPMSENSE_KEYRING	= (1 << 26),
    RPMSENSE_STRONG	= (1 << 27),
    RPMSENSE_CONFIG	= (1 << 28)
} rpmsenseFlags;

#define	RPMSENSE_SENSEMASK	15	 /* Mask to get senses, ie serial, */
                                         /* less, greater, equal.          */

#define	RPMSENSE_TRIGGER	\
	(RPMSENSE_TRIGGERPREIN | RPMSENSE_TRIGGERIN | RPMSENSE_TRIGGERUN | RPMSENSE_TRIGGERPOSTUN)

#define	_ALL_REQUIRES_MASK	(\
    RPMSENSE_INTERP | \
    RPMSENSE_SCRIPT_PRE | \
    RPMSENSE_SCRIPT_POST | \
    RPMSENSE_SCRIPT_PREUN | \
    RPMSENSE_SCRIPT_POSTUN | \
    RPMSENSE_SCRIPT_VERIFY | \
    RPMSENSE_FIND_REQUIRES | \
    RPMSENSE_STRONG | \
    RPMSENSE_SCRIPT_PREP | \
    RPMSENSE_SCRIPT_BUILD | \
    RPMSENSE_SCRIPT_INSTALL | \
    RPMSENSE_SCRIPT_CLEAN | \
    RPMSENSE_RPMLIB | \
    RPMSENSE_KEYRING )

#define	_notpre(_x)		((_x) & ~RPMSENSE_PREREQ)
#define	_INSTALL_ONLY_MASK \
    _notpre(RPMSENSE_SCRIPT_PRE|RPMSENSE_SCRIPT_POST|RPMSENSE_RPMLIB|RPMSENSE_KEYRING)
#define	_ERASE_ONLY_MASK  \
    _notpre(RPMSENSE_SCRIPT_PREUN|RPMSENSE_SCRIPT_POSTUN)

#define	isLegacyPreReq(_x)  (((_x) & _ALL_REQUIRES_MASK) == RPMSENSE_PREREQ)
#define	isInstallPreReq(_x)	((_x) & _INSTALL_ONLY_MASK)
#define	isErasePreReq(_x)	((_x) & _ERASE_ONLY_MASK)

/** \ingroup rpmds
 * Unreference a dependency set instance.
 * @param ds		dependency set
 * @param msg
 * @return		NULL always
 */
rpmds rpmdsUnlink (rpmds ds, const char * msg);
/** \ingroup rpmds
 * Reference a dependency set instance.
 * @param ds		dependency set
 * @param msg
 * @return		new dependency set reference
 */
rpmds rpmdsLink (rpmds ds, const char * msg);

/** \ingroup rpmds
 * Destroy a dependency set.
 * @param ds		dependency set
 * @return		NULL always
 */
rpmds rpmdsFree(rpmds ds);
/** \ingroup rpmds
 * Create and load a dependency set.
 * @deprecated Only scareMem = 0 will be permitted.
 * @param h		header
 * @param tagN		type of dependency
 * @param flags		scareMem(0x1)
 * @return		new dependency set
 */
rpmds rpmdsNew(Header h, rpmTag tagN, int flags);

/** \ingroup rpmds
 * Return new formatted dependency string.
 * @param dspfx		formatted dependency string prefix
 * @param ds		dependency set
 * @return		new formatted dependency (malloc'ed)
 */
char * rpmdsNewDNEVR(const char * dspfx, const rpmds ds);

/** \ingroup rpmds
 * Create, load and initialize a dependency for this header. 
 * @param h		header
 * @param tagN		type of dependency
 * @param Flags		comparison flags
 * @return		new dependency set
 */
rpmds rpmdsThis(Header h, rpmTag tagN, rpmsenseFlags Flags);

/** \ingroup rpmds
 * Create, load and initialize a dependency set of size 1.
 * @param tagN		type of dependency
 * @param N		name
 * @param EVR		epoch:version-release
 * @param Flags		comparison flags
 * @return		new dependency set
 */
rpmds rpmdsSingle(rpmTag tagN, const char * N, const char * EVR, rpmsenseFlags Flags);

/** \ingroup rpmds
 * Return dependency set count.
 * @param ds		dependency set
 * @return		current count
 */
int rpmdsCount(const rpmds ds);

/** \ingroup rpmds
 * Return dependency set index.
 * @param ds		dependency set
 * @return		current index
 */
int rpmdsIx(const rpmds ds);

/** \ingroup rpmds
 * Set dependency set index.
 * @param ds		dependency set
 * @param ix		new index
 * @return		current index
 */
int rpmdsSetIx(rpmds ds, int ix);

/** \ingroup rpmds
 * Return current formatted dependency string.
 * @param ds		dependency set
 * @return		current dependency DNEVR, NULL on invalid
 */
const char * rpmdsDNEVR(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency name.
 * @param ds		dependency set
 * @return		current dependency name, NULL on invalid
 */
const char * rpmdsN(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency epoch-version-release.
 * @param ds		dependency set
 * @return		current dependency EVR, NULL on invalid
 */
const char * rpmdsEVR(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency flags.
 * @param ds		dependency set
 * @return		current dependency flags, 0 on invalid
 */
rpmsenseFlags rpmdsFlags(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency type.
 * @param ds		dependency set
 * @return		current dependency type, 0 on invalid
 */
rpmTag rpmdsTagN(const rpmds ds);

/** \ingroup rpmds
 * Return dependency build time.
 * @param ds		dependency set
 * @return		dependency build time, 0 on invalid
 */
time_t rpmdsBT(const rpmds ds);

/** \ingroup rpmds
 * Set dependency build time.
 * @param ds		dependency set
 * @param BT		build time
 * @return		dependency build time, 0 on invalid
 */
time_t rpmdsSetBT(const rpmds ds, time_t BT);

/** \ingroup rpmds
 * Return current "Don't promote Epoch:" flag.
 *
 * This flag controls for Epoch: promotion when a dependency set is
 * compared. If the flag is set (for already installed packages), then
 * an unspecified value will be treated as Epoch: 0. Otherwise (for added
 * packages), the Epoch: portion of the comparison is skipped if the value
 * is not specified, i.e. an unspecified Epoch: is assumed to be equal
 * in dependency comparisons.
 *
 * @param ds		dependency set
 * @return		current "Don't promote Epoch:" flag
 */
int rpmdsNoPromote(const rpmds ds);

/** \ingroup rpmds
 * Set "Don't promote Epoch:" flag.
 * @param ds		dependency set
 * @param nopromote	Should an unspecified Epoch: be treated as Epoch: 0?
 * @return		previous "Don't promote Epoch:" flag
 */
int rpmdsSetNoPromote(rpmds ds, int nopromote);

/** \ingroup rpmds
 * Return current dependency color.
 * @param ds		dependency set
 * @return		current dependency color
 */
rpm_color_t rpmdsColor(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency color.
 * @param ds		dependency set
 * @param color		new dependency color
 * @return		previous dependency color
 */
rpm_color_t rpmdsSetColor(const rpmds ds, rpm_color_t color);

/** \ingroup rpmds
 * Return current dependency file refs.
 * @param ds		dependency set
 * @return		current dependency file refs, -1 on global
 */
int32_t rpmdsRefs(const rpmds ds);

/** \ingroup rpmds
 * Return current dependency color.
 * @param ds		dependency set
 * @param refs		new dependency refs
 * @return		previous dependency refs
 */
int32_t rpmdsSetRefs(const rpmds ds, int32_t refs);

/** \ingroup rpmds
 * Notify of results of dependency match.
 * @param ds		dependency set
 * @param where		where dependency was resolved (or NULL)
 * @param rc		0 == YES, otherwise NO
 */
/* FIX: rpmMessage annotation is a lie */
void rpmdsNotify(rpmds ds, const char * where, int rc);

/** \ingroup rpmds
 * Return next dependency set iterator index.
 * @param ds		dependency set
 * @return		dependency set iterator index, -1 on termination
 */
int rpmdsNext(rpmds ds);

/** \ingroup rpmds
 * Initialize dependency set iterator.
 * @param ds		dependency set
 * @return		dependency set
 */
rpmds rpmdsInit(rpmds ds);

/** \ingroup rpmds
 * Find a dependency set element using binary search.
 * @param ds		dependency set to search
 * @param ods		dependency set element to find.
 * @return		dependency index (or -1 if not found)
 */
int rpmdsFind(rpmds ds, const rpmds ods);

/** \ingroup rpmds
 * Merge a dependency set maintaining (N,EVR,Flags) sorted order.
 * @retval *dsp		(merged) dependency set
 * @param ods		dependency set to merge
 * @return		(merged) dependency index
 */
int rpmdsMerge(rpmds * dsp, rpmds ods);

/** \ingroup rpmds
 * Search a sorted dependency set for an element that overlaps.
 * A boolean result is saved (if allocated) and accessible through
 * rpmdsResult(ods) afterwards.
 * @param ds            dependency set to search
 * @param ods           dependency set element to find.
 * @return              dependency index (or -1 if not found)
 **/
int rpmdsSearch(rpmds ds, rpmds ods);


/** \ingroup rpmds
 * Compare two versioned dependency ranges, looking for overlap.
 * @param A		1st dependency
 * @param B		2nd dependency
 * @return		1 if dependencies overlap, 0 otherwise
 */
int rpmdsCompare(const rpmds A, const rpmds B);

/** \ingroup rpmds
 * Report a Requires: or Conflicts: dependency problem.
 * @param ps		transaction set problems
 * @param pkgNEVR	package name/epoch/version/release
 * @param ds		dependency set
 * @param suggestedKeys	filename or python object address
 * @param adding	dependency problem is from added package set?
 */
void rpmdsProblem(rpmps ps, const char * pkgNEVR, const rpmds ds,
		const fnpyKey * suggestedKeys,
		int adding);

/** \ingroup rpmds
 * Compare package provides dependencies from header with a single dependency.
 * @param h		header
 * @param req		dependency set
 * @param nopromote	Don't promote Epoch: in comparison?
 * @return		1 if any dependency overlaps, 0 otherwise
 */
int rpmdsAnyMatchesDep (const Header h, const rpmds req, int nopromote);

/** \ingroup rpmds
 * Compare package name-version-release from header with a single dependency.
 * @param h		header
 * @param req		dependency set
 * @param nopromote	Don't promote Epoch: in comparison?
 * @return		1 if dependency overlaps, 0 otherwise
 */
int rpmdsNVRMatchesDep(const Header h, const rpmds req, int nopromote);

/**
 * Load rpmlib provides into a dependency set.
 * @retval *dsp		(loaded) depedency set
 * @param tblp		rpmlib provides table (NULL uses internal table)
 * @return		0 on success
 */
int rpmdsRpmlib(rpmds * dsp, void * tblp);

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMDS */
