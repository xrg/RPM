/** \ingroup rpmdep
 * \file lib/rpmds.c
 */
#include "system.h"

#include <rpm/rpmtypes.h>
#include <rpm/rpmlib.h>		/* rpmvercmp */
#include <rpm/rpmstring.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmds.h>

#include "debug.h"

int _rpmds_debug = 0;

int _rpmds_nopromote = 1;

/**
 * A package dependency set.
 */
struct rpmds_s {
    const char * Type;		/*!< Tag name. */
    char * DNEVR;		/*!< Formatted dependency string. */
    const char ** N;		/*!< Name. */
    const char ** EVR;		/*!< Epoch-Version-Release. */
    rpmsenseFlags * Flags;	/*!< Bit(s) identifying context/comparison. */
    rpm_color_t * Color;	/*!< Bit(s) calculated from file color(s). */
    rpmTagVal tagN;		/*!< Header tag. */
    int32_t Count;		/*!< No. of elements */
    unsigned int instance;	/*!< From rpmdb instance? */
    int i;			/*!< Element index. */
    unsigned l;			/*!< Low element (bsearch). */
    unsigned u;			/*!< High element (bsearch). */
    int nopromote;		/*!< Don't promote Epoch: in rpmdsCompare()? */
    int nrefs;			/*!< Reference count. */
};

static const char ** rpmdsDupArgv(const char ** argv, int argc);

static int dsType(rpmTagVal tag, 
		  const char ** Type, rpmTagVal * tagEVR, rpmTagVal * tagF)
{
    int rc = 0;
    const char *t = NULL;
    rpmTagVal evr = RPMTAG_NOT_FOUND;
    rpmTagVal f = RPMTAG_NOT_FOUND;

    if (tag == RPMTAG_PROVIDENAME) {
	t = "Provides";
	evr = RPMTAG_PROVIDEVERSION;
	f = RPMTAG_PROVIDEFLAGS;
    } else if (tag == RPMTAG_REQUIRENAME) {
	t = "Requires";
	evr = RPMTAG_REQUIREVERSION;
	f = RPMTAG_REQUIREFLAGS;
    } else if (tag == RPMTAG_CONFLICTNAME) {
	t = "Conflicts";
	evr = RPMTAG_CONFLICTVERSION;
	f = RPMTAG_CONFLICTFLAGS;
    } else if (tag == RPMTAG_OBSOLETENAME) {
	t = "Obsoletes";
	evr = RPMTAG_OBSOLETEVERSION;
	f = RPMTAG_OBSOLETEFLAGS;
    } else if (tag == RPMTAG_ORDERNAME) {
	t = "Order";
	evr = RPMTAG_ORDERVERSION;
	f = RPMTAG_ORDERFLAGS;
    } else if (tag == RPMTAG_TRIGGERNAME) {
	t = "Trigger";
	evr = RPMTAG_TRIGGERVERSION;
	f = RPMTAG_TRIGGERFLAGS;
    } else if (tag == RPMTAG_ENHANCESNAME) {
	t = "Enhances";
	evr = RPMTAG_ENHANCESVERSION;
	f = RPMTAG_ENHANCESFLAGS;
    } else {
	rc = 1;
    } 
    if (Type) *Type = t;
    if (tagEVR) *tagEVR = evr;
    if (tagF) *tagF = f;
    return rc;
}    

static rpmds rpmdsUnlink(rpmds ds)
{
    if (ds)
	ds->nrefs--;
    return NULL;
}

rpmds rpmdsLink(rpmds ds)
{
    if (ds)
	ds->nrefs++;
    return ds;
}

rpmds rpmdsFree(rpmds ds)
{
    rpmTagVal tagEVR, tagF;

    if (ds == NULL)
	return NULL;

    if (ds->nrefs > 1)
	return rpmdsUnlink(ds);

    if (dsType(ds->tagN, NULL, &tagEVR, &tagF))
	return NULL;

    if (ds->Count > 0) {
	ds->N = _free(ds->N);
	ds->EVR = _free(ds->EVR);
	ds->Flags = _free(ds->Flags);
    }

    ds->DNEVR = _free(ds->DNEVR);
    ds->Color = _free(ds->Color);

    (void) rpmdsUnlink(ds);
    memset(ds, 0, sizeof(*ds));		/* XXX trash and burn */
    ds = _free(ds);
    return NULL;
}

rpmds rpmdsNew(Header h, rpmTagVal tagN, int flags)
{
    rpmTagVal tagEVR, tagF;
    rpmds ds = NULL;
    const char * Type;
    struct rpmtd_s names;
    headerGetFlags hgflags = HEADERGET_ALLOC|HEADERGET_ARGV;

    if (dsType(tagN, &Type, &tagEVR, &tagF))
	goto exit;

    if (headerGet(h, tagN, &names, hgflags) && rpmtdCount(&names) > 0) {
	struct rpmtd_s evr, flags; 

	ds = xcalloc(1, sizeof(*ds));
	ds->Type = Type;
	ds->i = -1;
	ds->DNEVR = NULL;
	ds->tagN = tagN;
	ds->N = names.data;
	ds->Count = rpmtdCount(&names);
	ds->nopromote = _rpmds_nopromote;
	ds->instance = headerGetInstance(h);

	headerGet(h, tagEVR, &evr, hgflags);
	ds->EVR = evr.data;
	headerGet(h, tagF, &flags, hgflags);
	ds->Flags = flags.data;
	/* ensure rpmlib() requires always have RPMSENSE_RPMLIB flag set */
	if (tagN == RPMTAG_REQUIRENAME && ds->Flags) {
	    for (int i = 0; i < ds->Count; i++) {
		if (!(ds->Flags[i] & RPMSENSE_RPMLIB) &&
			rstreqn(ds->N[i], "rpmlib(", sizeof("rpmlib(")-1))
		    ds->Flags[i] |= RPMSENSE_RPMLIB;
	    }
	}

	ds = rpmdsLink(ds);
    }

exit:
    return ds;
}

char * rpmdsNewDNEVR(const char * dspfx, const rpmds ds)
{
    char * tbuf, * t;
    size_t nb;

    nb = 0;
    if (dspfx)	nb += strlen(dspfx) + 1;
    if (ds->N[ds->i])	nb += strlen(ds->N[ds->i]);
    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
    if (ds->Flags != NULL && (ds->Flags[ds->i] & RPMSENSE_SENSEMASK)) {
	if (nb)	nb++;
	if (ds->Flags[ds->i] & RPMSENSE_LESS)	nb++;
	if (ds->Flags[ds->i] & RPMSENSE_GREATER) nb++;
	if (ds->Flags[ds->i] & RPMSENSE_EQUAL)	nb++;
    }
    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
    if (ds->EVR != NULL && ds->EVR[ds->i] && *ds->EVR[ds->i]) {
	if (nb)	nb++;
	nb += strlen(ds->EVR[ds->i]);
    }

    t = tbuf = xmalloc(nb + 1);
    if (dspfx) {
	t = stpcpy(t, dspfx);
	*t++ = ' ';
    }
    if (ds->N[ds->i])
	t = stpcpy(t, ds->N[ds->i]);
    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
    if (ds->Flags != NULL && (ds->Flags[ds->i] & RPMSENSE_SENSEMASK)) {
	if (t != tbuf)	*t++ = ' ';
	if (ds->Flags[ds->i] & RPMSENSE_LESS)	*t++ = '<';
	if (ds->Flags[ds->i] & RPMSENSE_GREATER) *t++ = '>';
	if (ds->Flags[ds->i] & RPMSENSE_EQUAL)	*t++ = '=';
    }
    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
    if (ds->EVR != NULL && ds->EVR[ds->i] && *ds->EVR[ds->i]) {
	if (t != tbuf)	*t++ = ' ';
	t = stpcpy(t, ds->EVR[ds->i]);
    }
    *t = '\0';
    return tbuf;
}

static rpmds singleDS(rpmTagVal tagN, const char * N, const char * EVR,
		      rpmsenseFlags Flags, unsigned int instance,
		      rpm_color_t Color)
{
    rpmds ds = NULL;
    const char * Type;

    if (dsType(tagN, &Type, NULL, NULL))
	goto exit;

    ds = xcalloc(1, sizeof(*ds));
    ds->Type = Type;
    ds->tagN = tagN;
    ds->Count = 1;
    ds->nopromote = _rpmds_nopromote;
    ds->instance = instance;

    ds->N = rpmdsDupArgv(&N, 1);
    ds->EVR = rpmdsDupArgv(&EVR, 1);

    ds->Flags = xmalloc(sizeof(*ds->Flags));
    ds->Flags[0] = Flags;
    ds->i = 0;
    if (Color)
	rpmdsSetColor(ds, Color);

exit:
    return rpmdsLink(ds);
}

rpmds rpmdsThis(Header h, rpmTagVal tagN, rpmsenseFlags Flags)
{
    char *evr = headerGetAsString(h, RPMTAG_EVR);
    rpmds ds = singleDS(tagN, headerGetString(h, RPMTAG_NAME),
			evr, Flags, headerGetInstance(h), 0);
    free(evr);
    return ds;
}

rpmds rpmdsSingle(rpmTagVal tagN, const char * N, const char * EVR, rpmsenseFlags Flags)
{
    return singleDS(tagN, N, EVR, Flags, 0, 0);
}

rpmds rpmdsCurrent(rpmds ds)
{
    rpmds cds = NULL;
    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	cds = singleDS(ds->tagN, ds->N[ds->i], ds->EVR[ds->i],
		       ds->Flags[ds->i], ds->instance, rpmdsColor(ds));
    }
    return cds;
}

int rpmdsCount(const rpmds ds)
{
    return (ds != NULL ? ds->Count : 0);
}

int rpmdsIx(const rpmds ds)
{
    return (ds != NULL ? ds->i : -1);
}

int rpmdsSetIx(rpmds ds, int ix)
{
    int i = -1;

    if (ds != NULL) {
	i = ds->i;
	ds->i = ix;
	ds->DNEVR = _free(ds->DNEVR);
    }
    return i;
}

const char * rpmdsDNEVR(const rpmds ds)
{
    const char * DNEVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->DNEVR == NULL) {
	    char t[2] = { ds->Type[0], '\0' };
	    ds->DNEVR = rpmdsNewDNEVR(t, ds);
	}
	DNEVR = ds->DNEVR;
    }
    return DNEVR;
}

const char * rpmdsN(const rpmds ds)
{
    const char * N = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->N != NULL)
	    N = ds->N[ds->i];
    }
    return N;
}

const char * rpmdsEVR(const rpmds ds)
{
    const char * EVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->EVR != NULL)
	    EVR = ds->EVR[ds->i];
    }
    return EVR;
}

rpmsenseFlags rpmdsFlags(const rpmds ds)
{
    rpmsenseFlags Flags = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->Flags != NULL)
	    Flags = ds->Flags[ds->i];
    }
    return Flags;
}

rpmTagVal rpmdsTagN(const rpmds ds)
{
    rpmTagVal tagN = RPMTAG_NOT_FOUND;

    if (ds != NULL)
	tagN = ds->tagN;
    return tagN;
}

unsigned int rpmdsInstance(rpmds ds)
{
    return (ds != NULL) ? ds->instance : 0;
}

int rpmdsNoPromote(const rpmds ds)
{
    int nopromote = 0;

    if (ds != NULL)
	nopromote = ds->nopromote;
    return nopromote;
}

int rpmdsSetNoPromote(rpmds ds, int nopromote)
{
    int onopromote = 0;

    if (ds != NULL) {
	onopromote = ds->nopromote;
	ds->nopromote = nopromote;
    }
    return onopromote;
}

rpm_color_t rpmdsColor(const rpmds ds)
{
    rpm_color_t Color = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->Color != NULL)
	    Color = ds->Color[ds->i];
    }
    return Color;
}

rpm_color_t rpmdsSetColor(const rpmds ds, rpm_color_t color)
{
    rpm_color_t ocolor = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->Color == NULL) {
	    ds->Color = xcalloc(ds->Count, sizeof(*ds->Color));
	}
	ocolor = ds->Color[ds->i];
	ds->Color[ds->i] = color;
    }
    return ocolor;
}

void rpmdsNotify(rpmds ds, const char * where, int rc)
{
    const char *DNEVR;

    if (!rpmIsDebug())
	return;
    if (!(ds != NULL && ds->i >= 0 && ds->i < ds->Count))
	return;
    if (!(ds->Type != NULL && (DNEVR = rpmdsDNEVR(ds)) != NULL))
	return;

    rpmlog(RPMLOG_DEBUG, "%9s: %-45s %-s %s\n", ds->Type,
		(rstreq(DNEVR, "cached") ? DNEVR : DNEVR+2),
		(rc ? _("NO ") : _("YES")),
		(where != NULL ? where : ""));
}

int rpmdsNext(rpmds ds)
{
    int i = -1;

    if (ds != NULL && ++ds->i >= 0) {
	if (ds->i < ds->Count) {
	    i = ds->i;
	    ds->DNEVR = _free(ds->DNEVR);
	} else
	    ds->i = -1;

if (_rpmds_debug  < 0 && i != -1)
fprintf(stderr, "*** ds %p\t%s[%d]: %s\n", ds, (ds->Type ? ds->Type : "?Type?"), i, (ds->DNEVR ? ds->DNEVR : "?DNEVR?"));

    }

    return i;
}

rpmds rpmdsInit(rpmds ds)
{
    if (ds != NULL) {
	ds->i = -1;
	ds->DNEVR = _free(ds->DNEVR);
    }
    return ds;
}

static
const char ** rpmdsDupArgv(const char ** argv, int argc)
{
    const char ** av;
    size_t nb = 0;
    int ac = 0;
    char * t;

    if (argv == NULL)
	return NULL;
    for (ac = 0; ac < argc && argv[ac]; ac++) {
	nb += strlen(argv[ac]) + 1;
    }
    nb += (ac + 1) * sizeof(*av);

    av = xmalloc(nb);
    t = (char *) (av + ac + 1);
    for (ac = 0; ac < argc && argv[ac]; ac++) {
	av[ac] = t;
	t = stpcpy(t, argv[ac]) + 1;
    }
    av[ac] = NULL;
    return av;
}

static rpmds rpmdsDup(const rpmds ods)
{
    rpmds ds = xcalloc(1, sizeof(*ds));
    size_t nb;

    ds->Type = ods->Type;
    ds->tagN = ods->tagN;
    ds->Count = ods->Count;
    ds->i = ods->i;
    ds->l = ods->l;
    ds->u = ods->u;
    ds->nopromote = ods->nopromote;

    ds->N = rpmdsDupArgv(ods->N, ods->Count);

    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
assert(ods->EVR != NULL);
assert(ods->Flags != NULL);

    ds->EVR = rpmdsDupArgv(ods->EVR, ods->Count);

    nb = (ds->Count * sizeof(*ds->Flags));
    ds->Flags = memcpy(xmalloc(nb), ods->Flags, nb);

    return rpmdsLink(ds);

}

int rpmdsFind(rpmds ds, const rpmds ods)
{
    int comparison;

    if (ds == NULL || ods == NULL)
	return -1;

    ds->l = 0;
    ds->u = ds->Count;
    while (ds->l < ds->u) {
	ds->i = (ds->l + ds->u) / 2;

	comparison = strcmp(ods->N[ods->i], ds->N[ds->i]);

	/* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
	if (comparison == 0 && ods->EVR && ds->EVR)
	    comparison = strcmp(ods->EVR[ods->i], ds->EVR[ds->i]);
	if (comparison == 0 && ods->Flags && ds->Flags)
	    comparison = (ods->Flags[ods->i] - ds->Flags[ds->i]);

	if (comparison < 0)
	    ds->u = ds->i;
	else if (comparison > 0)
	    ds->l = ds->i + 1;
	else
	    return ds->i;
    }
    return -1;
}

int rpmdsMerge(rpmds * dsp, rpmds ods)
{
    rpmds ds;
    const char ** N;
    const char ** EVR;
    rpmsenseFlags * Flags;
    int j;
    int save;

    if (dsp == NULL || ods == NULL)
	return -1;

    /* If not initialized yet, dup the 1st entry. */
    if (*dsp == NULL) {
	save = ods->Count;
	ods->Count = 1;
	*dsp = rpmdsDup(ods);
	ods->Count = save;
    }
    ds = *dsp;
    if (ds == NULL)
	return -1;

    /*
     * Add new entries.
     */
    save = ods->i;
    ods = rpmdsInit(ods);
    while (rpmdsNext(ods) >= 0) {
	/*
	 * If this entry is already present, don't bother.
	 */
	if (rpmdsFind(ds, ods) >= 0)
	    continue;

	/*
	 * Insert new entry.
	 */
	for (j = ds->Count; j > ds->u; j--)
	    ds->N[j] = ds->N[j-1];
	ds->N[ds->u] = ods->N[ods->i];
	N = rpmdsDupArgv(ds->N, ds->Count+1);
	ds->N = _free(ds->N);
	ds->N = N;
	
	/* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
assert(ods->EVR != NULL);
assert(ods->Flags != NULL);

	for (j = ds->Count; j > ds->u; j--)
	    ds->EVR[j] = ds->EVR[j-1];
	ds->EVR[ds->u] = ods->EVR[ods->i];
	EVR = rpmdsDupArgv(ds->EVR, ds->Count+1);
	ds->EVR = _free(ds->EVR);
	ds->EVR = EVR;

	Flags = xmalloc((ds->Count+1) * sizeof(*Flags));
	if (ds->u > 0)
	    memcpy(Flags, ds->Flags, ds->u * sizeof(*Flags));
	if (ds->u < ds->Count)
	    memcpy(Flags + ds->u + 1, ds->Flags + ds->u, 
		   (ds->Count - ds->u) * sizeof(*Flags));
	Flags[ds->u] = ods->Flags[ods->i];
	ds->Flags = _free(ds->Flags);
	ds->Flags = Flags;

	ds->i = ds->Count;
	ds->Count++;

    }
    ods->i = save;
    return 0;
}


int rpmdsSearch(rpmds ds, rpmds ods)
{
    int comparison;
    int i, l, u;

    if (ds == NULL || ods == NULL)
	return -1;

    /* Binary search to find the [l,u) subset that contains N */
    i = -1;
    l = 0;
    u = ds->Count;
    while (l < u) {
	i = (l + u) / 2;

	comparison = strcmp(ods->N[ods->i], ds->N[i]);

	if (comparison < 0)
	    u = i;
	else if (comparison > 0)
	    l = i + 1;
	else {
	    /* Set l to 1st member of set that contains N. */
	    if (!rstreq(ods->N[ods->i], ds->N[l]))
		l = i;
	    while (l > 0 && rstreq(ods->N[ods->i], ds->N[l-1]))
		l--;
	    /* Set u to 1st member of set that does not contain N. */
	    if (u >= ds->Count || !rstreq(ods->N[ods->i], ds->N[u]))
		u = i;
	    while (++u < ds->Count) {
		if (!rstreq(ods->N[ods->i], ds->N[u]))
		    /*@innerbreak@*/ break;
	    }
	    break;
	}
    }

    /* Check each member of [l,u) subset for ranges overlap. */
    i = -1;
    if (l < u) {
	int save = rpmdsSetIx(ds, l-1);
	while ((l = rpmdsNext(ds)) >= 0 && (l < u)) {
	    if ((i = rpmdsCompare(ods, ds)) != 0)
		break;
	}
	/* Return element index that overlaps, or -1. */
	if (i)
	    i = rpmdsIx(ds);
	else {
	    (void) rpmdsSetIx(ds, save);
	    i = -1;
	}
    }
    return i;
}
/**
 * Split EVR into epoch, version, and release components.
 * @param evr		[epoch:]version[-release] string
 * @retval *ep		pointer to epoch
 * @retval *vp		pointer to version
 * @retval *rp		pointer to release
 */
static
void parseEVR(char * evr,
		const char ** ep,
		const char ** vp,
		const char ** rp)
{
    const char *epoch;
    const char *version;		/* assume only version is present */
    const char *release;
    char *s, *se;

    s = evr;
    while (*s && risdigit(*s)) s++;	/* s points to epoch terminator */
    se = strrchr(s, '-');		/* se points to version terminator */

    if (*s == ':') {
	epoch = evr;
	*s++ = '\0';
	version = s;
	if (*epoch == '\0') epoch = "0";
    } else {
	epoch = NULL;	/* XXX disable epoch compare if missing */
	version = evr;
    }
    if (se) {
	*se++ = '\0';
	release = se;
    } else {
	release = NULL;
    }

    if (ep) *ep = epoch;
    if (vp) *vp = version;
    if (rp) *rp = release;
}

int rpmdsCompare(const rpmds A, const rpmds B)
{
    char *aEVR, *bEVR;
    const char *aE, *aV, *aR, *bE, *bV, *bR;
    int result;
    int sense;

    /* Different names don't overlap. */
    if (!rstreq(A->N[A->i], B->N[B->i])) {
	result = 0;
	goto exit;
    }

    /* XXX rpm prior to 3.0.2 did not always supply EVR and Flags. */
    if (!(A->EVR && A->Flags && B->EVR && B->Flags)) {
	result = 1;
	goto exit;
    }

    /* Same name. If either A or B is an existence test, always overlap. */
    if (!((A->Flags[A->i] & RPMSENSE_SENSEMASK) && (B->Flags[B->i] & RPMSENSE_SENSEMASK))) {
	result = 1;
	goto exit;
    }

    /* If either EVR is non-existent or empty, always overlap. */
    if (!(A->EVR[A->i] && *A->EVR[A->i] && B->EVR[B->i] && *B->EVR[B->i])) {
	result = 1;
	goto exit;
    }

    /* Both AEVR and BEVR exist. */
    aEVR = xstrdup(A->EVR[A->i]);
    parseEVR(aEVR, &aE, &aV, &aR);
    bEVR = xstrdup(B->EVR[B->i]);
    parseEVR(bEVR, &bE, &bV, &bR);

    /* Compare {A,B} [epoch:]version[-release] */
    sense = 0;
    if (aE && *aE && bE && *bE)
	sense = rpmvercmp(aE, bE);
    else if (aE && *aE && atol(aE) > 0) {
	if (!B->nopromote) {
	    sense = 0;
	} else
	    sense = 1;
    } else if (bE && *bE && atol(bE) > 0)
	sense = -1;

    if (sense == 0) {
	sense = rpmvercmp(aV, bV);
	if (sense == 0) {
	    if (aR && *aR && bR && *bR) {
		sense = rpmvercmp(aR, bR);
	    } else {
		/* always matches if the side with no release has SENSE_EQUAL */
		if ((aR && *aR && (B->Flags[B->i] & RPMSENSE_EQUAL)) ||
		    (bR && *bR && (A->Flags[A->i] & RPMSENSE_EQUAL))) {
		    aEVR = _free(aEVR);
		    bEVR = _free(bEVR);
		    result = 1;
		    goto exit;
		}
	    }
	}
    }
    aEVR = _free(aEVR);
    bEVR = _free(bEVR);

    /* Detect overlap of {A,B} range. */
    result = 0;
    if (sense < 0 && ((A->Flags[A->i] & RPMSENSE_GREATER) || (B->Flags[B->i] & RPMSENSE_LESS))) {
	result = 1;
    } else if (sense > 0 && ((A->Flags[A->i] & RPMSENSE_LESS) || (B->Flags[B->i] & RPMSENSE_GREATER))) {
	result = 1;
    } else if (sense == 0 &&
	(((A->Flags[A->i] & RPMSENSE_EQUAL) && (B->Flags[B->i] & RPMSENSE_EQUAL)) ||
	 ((A->Flags[A->i] & RPMSENSE_LESS) && (B->Flags[B->i] & RPMSENSE_LESS)) ||
	 ((A->Flags[A->i] & RPMSENSE_GREATER) && (B->Flags[B->i] & RPMSENSE_GREATER)))) {
	result = 1;
    }

exit:
    return result;
}

int rpmdsMatchesDep (const Header h, int ix, const rpmds req, int nopromote)
{
    /* Get provides information from header */
    rpmds provides = rpmdsInit(rpmdsNew(h, RPMTAG_PROVIDENAME, 0));
    int result = 0;

    rpmdsSetIx(provides,ix);
    result = rpmdsCompare(provides, req);

    rpmdsFree(provides);
    return result;
}

int rpmdsAnyMatchesDep (const Header h, const rpmds req, int nopromote)
{
    rpmds provides = NULL;
    int result = 0;

    /* Get provides information from header */
    provides = rpmdsInit(rpmdsNew(h, RPMTAG_PROVIDENAME, 0));
    if (provides == NULL)
	goto exit;	/* XXX should never happen */

    (void) rpmdsSetNoPromote(provides, nopromote);

    while (rpmdsNext(provides) >= 0) {

	result = rpmdsCompare(provides, req);

	/* If this provide matches the require, we're done. */
	if (result)
	    break;
    }

exit:
    rpmdsFree(provides);

    return result;
}

int rpmdsNVRMatchesDep(const Header h, const rpmds req, int nopromote)
{
    rpmds pkg;
    int rc = 1;	/* XXX assume match, names already match here */

    /* Get package information from header */
    pkg = rpmdsThis(h, RPMTAG_PROVIDENAME, RPMSENSE_EQUAL);
    rpmdsSetNoPromote(pkg, nopromote);
    rc = rpmdsCompare(pkg, req);
    rpmdsFree(pkg);

    return rc;
}

/**
 */
struct rpmlibProvides_s {
    const char * featureName;
    const char * featureEVR;
    rpmsenseFlags featureFlags;
    const char * featureDescription;
};

static const struct rpmlibProvides_s rpmlibProvides[] = {
    { "rpmlib(VersionedDependencies)",	"3.0.3-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("PreReq:, Provides:, and Obsoletes: dependencies support versions.") },
    { "rpmlib(CompressedFileNames)",	"3.0.4-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("file name(s) stored as (dirName,baseName,dirIndex) tuple, not as path.")},
#if HAVE_BZLIB_H
    { "rpmlib(PayloadIsBzip2)",		"3.0.5-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload can be compressed using bzip2.") },
#endif
#if HAVE_LZMA_H
    { "rpmlib(PayloadIsXz)",		"5.2-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload can be compressed using xz.") },
    { "rpmlib(PayloadIsLzma)",		"4.4.2-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload can be compressed using lzma.") },
#endif
    { "rpmlib(PayloadFilesHavePrefix)",	"4.0-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload file(s) have \"./\" prefix.") },
    { "rpmlib(ExplicitPackageProvide)",	"4.0-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package name-version-release is not implicitly provided.") },
    { "rpmlib(HeaderLoadSortsTags)",    "4.0.1-1",
	(                RPMSENSE_EQUAL),
    N_("header tags are always sorted after being loaded.") },
    { "rpmlib(ScriptletInterpreterArgs)",    "4.0.3-1",
	(                RPMSENSE_EQUAL),
    N_("the scriptlet interpreter can use arguments from header.") },
    { "rpmlib(PartialHardlinkSets)",    "4.0.4-1",
	(                RPMSENSE_EQUAL),
    N_("a hardlink file set may be installed without being complete.") },
    { "rpmlib(ConcurrentAccess)",    "4.1-1",
	(                RPMSENSE_EQUAL),
    N_("package scriptlets may access the rpm database while installing.") },
#ifdef WITH_LUA
    { "rpmlib(BuiltinLuaScripts)",    "4.2.2-1",
	(                RPMSENSE_EQUAL),
    N_("internal support for lua scripts.") },
#endif
    { "rpmlib(FileDigests)", 		"4.6.0-1",
	(		 RPMSENSE_EQUAL),
    N_("file digest algorithm is per package configurable") },
#ifdef WITH_CAP
    { "rpmlib(FileCaps)", 		"4.6.1-1",
	(		 RPMSENSE_EQUAL),
    N_("support for POSIX.1e file capabilities") },
#endif
    { "rpmlib(ScriptletExpansion)",    "4.9.0-1",
	(		RPMSENSE_EQUAL),
    N_("package scriptlets can be expanded at install time.") },
    { "rpmlib(TildeInVersions)",    "4.10.0-1",
	(		RPMSENSE_EQUAL),
    N_("dependency comparison supports versions with tilde.") },
    { NULL,				NULL, 0,	NULL }
};


int rpmdsRpmlib(rpmds * dsp, const void * tblp)
{
    const struct rpmlibProvides_s * rltblp = tblp;
    const struct rpmlibProvides_s * rlp;
    int rc = 0;

    if (rltblp == NULL)
	rltblp = rpmlibProvides;

    for (rlp = rltblp; rlp->featureName != NULL && rc == 0; rlp++) {
	rpmds ds = rpmdsSingle(RPMTAG_PROVIDENAME, rlp->featureName,
			rlp->featureEVR, rlp->featureFlags);
	rc = rpmdsMerge(dsp, ds);
	rpmdsFree(ds);
    }
    return rc;
}

