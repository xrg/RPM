#include "system.h"

#include <stdarg.h>
#if defined(__linux__) && defined(__powerpc__)
#include <setjmp.h>
#endif

#include <ctype.h>	/* XXX for /etc/rpm/platform contents */

#if HAVE_SYS_SYSTEMCFG_H
#include <sys/systemcfg.h>
#else
#define __power_pc() 0
#endif

#include <rpm/rpmlib.h>			/* RPM_MACTABLE*, Rc-prototypes */
#include <rpm/rpmmacro.h>
#include <rpm/rpmfileutil.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmlog.h>

#include "rpmio/rpmlua.h"
#include "rpmio/rpmio_internal.h"	/* XXX for rpmioSlurp */

#include "debug.h"

static const char * const defrcfiles = 
      RPMCONFIGDIR "/rpmrc" 
  ":" RPMCONFIGDIR "/" "manbo" "/rpmrc"
  ":" SYSCONFDIR "/rpmrc"
  ":~/.rpmrc"; 

const char * macrofiles =
#ifndef MACROFILES
      RPMCONFIGDIR "/macros"
  ":" RPMCONFIGDIR "/platform/%{_target}/macros"
  ":" RPMCONFIGDIR "/" RPMCANONVENDOR "/macros"
  ":" SYSCONFDIR "/rpm/macros.*"
  ":" SYSCONFDIR "/rpm/macros"
  ":" SYSCONFDIR "/rpm/%{_target}/macros"
  ":~/.rpmmacros";
#else
  MACROFILES;
#endif

static const char * const platform = SYSCONFDIR "/rpm/platform";
static char ** platpat = NULL;
static int nplatpat = 0;

typedef char * cptr_t;

typedef struct machCacheEntry_s {
    char * name;
    int count;
    cptr_t * equivs;
    int visited;
} * machCacheEntry;

typedef struct machCache_s {
    machCacheEntry cache;
    int size;
} * machCache;

typedef struct machEquivInfo_s {
    char * name;
    int score;
} * machEquivInfo;

typedef struct machEquivTable_s {
    int count;
    machEquivInfo list;
} * machEquivTable;

struct rpmvarValue {
    char * value;
    /* eventually, this arch will be replaced with a generic condition */
    char * arch;
struct rpmvarValue * next;
};

struct rpmOption {
    char * name;
    int var;
    int archSpecific;
int required;
    int macroize;
    int localize;
struct rpmOptionValue * value;
};

typedef struct defaultEntry_s {
    char * name;
    char * defName;
} * defaultEntry;

typedef struct canonEntry_s {
    char * name;
    char * short_name;
    short num;
} * canonEntry;

/* tags are 'key'canon, 'key'translate, 'key'compat
 *
 * for giggles, 'key'_canon, 'key'_compat, and 'key'_canon will also work
 */
typedef struct tableType_s {
    char * const key;
    const int hasCanon;
    const int hasTranslate;
    struct machEquivTable_s equiv;
    struct machCache_s cache;
    defaultEntry defaults;
    canonEntry canons;
    int defaultsLength;
    int canonsLength;
} * tableType;

static struct tableType_s tables[RPM_MACHTABLE_COUNT] = {
    { "arch", 1, 0 },
    { "os", 1, 0 },
    { "buildarch", 0, 1 },
    { "buildos", 0, 1 }
};

/* XXX get rid of this stuff... */
/* Stuff for maintaining "variables" like SOURCEDIR, BUILDDIR, etc */
#define RPMVAR_OPTFLAGS                 3
#define RPMVAR_INCLUDE                  43
#define RPMVAR_MACROFILES               49

#define RPMVAR_NUM                      55      /* number of RPMVAR entries */

/* this *must* be kept in alphabetical order */
/* The order of the flags is archSpecific, required, macroize, localize */

static const struct rpmOption const optionTable[] = {
    { "include",		RPMVAR_INCLUDE,			0, 1,	0, 2 },
    { "macrofiles",		RPMVAR_MACROFILES,		0, 0,	0, 1 },
    { "optflags",		RPMVAR_OPTFLAGS,		1, 0,	1, 0 },
};

static const size_t optionTableSize = sizeof(optionTable) / sizeof(*optionTable);

#define OS	0
#define ARCH	1

static cptr_t current[2];

static int currTables[2] = { RPM_MACHTABLE_INSTOS, RPM_MACHTABLE_INSTARCH };

static struct rpmvarValue values[RPMVAR_NUM];

static int defaultsInitialized = 0;

/* prototypes */
static rpmRC doReadRC(const char * urlfn);

static void rpmSetVarArch(int var, const char * val,
		const char * arch);

static void rebuildCompatTables(int type, const char * name);

static void rpmRebuildTargetVars(const char **target, const char ** canontarget);

static int optionCompare(const void * a, const void * b)
{
    return rstrcasecmp(((const struct rpmOption *) a)->name,
		      ((const struct rpmOption *) b)->name);
}

static machCacheEntry
machCacheFindEntry(const machCache cache, const char * key)
{
    int i;

    for (i = 0; i < cache->size; i++)
	if (!strcmp(cache->cache[i].name, key)) return cache->cache + i;

    return NULL;
}

static int machCompatCacheAdd(char * name, const char * fn, int linenum,
				machCache cache)
{
    machCacheEntry entry = NULL;
    char * chptr;
    char * equivs;
    int delEntry = 0;
    int i;

    while (*name && risspace(*name)) name++;

    chptr = name;
    while (*chptr && *chptr != ':') chptr++;
    if (!*chptr) {
	rpmlog(RPMLOG_ERR, _("missing second ':' at %s:%d\n"), fn, linenum);
	return 1;
    } else if (chptr == name) {
	rpmlog(RPMLOG_ERR, _("missing architecture name at %s:%d\n"), fn,
			     linenum);
	return 1;
    }

    while (*chptr == ':' || risspace(*chptr)) chptr--;
    *(++chptr) = '\0';
    equivs = chptr + 1;
    while (*equivs && risspace(*equivs)) equivs++;
    if (!*equivs) {
	delEntry = 1;
    }

    if (cache->size) {
	entry = machCacheFindEntry(cache, name);
	if (entry) {
	    for (i = 0; i < entry->count; i++)
		entry->equivs[i] = _free(entry->equivs[i]);
	    entry->equivs = _free(entry->equivs);
	    entry->count = 0;
	}
    }

    if (!entry) {
	cache->cache = xrealloc(cache->cache,
			       (cache->size + 1) * sizeof(*cache->cache));
	entry = cache->cache + cache->size++;
	entry->name = xstrdup(name);
	entry->count = 0;
	entry->visited = 0;
    }

    if (delEntry) return 0;

    while ((chptr = strtok(equivs, " ")) != NULL) {
	equivs = NULL;
	if (chptr[0] == '\0')	/* does strtok() return "" ever?? */
	    continue;
	if (entry->count)
	    entry->equivs = xrealloc(entry->equivs, sizeof(*entry->equivs)
					* (entry->count + 1));
	else
	    entry->equivs = xmalloc(sizeof(*entry->equivs));

	entry->equivs[entry->count] = xstrdup(chptr);
	entry->count++;
    }

    return 0;
}

static machEquivInfo
machEquivSearch(const machEquivTable table, const char * name)
{
    int i;

    for (i = 0; i < table->count; i++)
	if (!rstrcasecmp(table->list[i].name, name))
	    return table->list + i;

    return NULL;
}

static void machAddEquiv(machEquivTable table, const char * name,
			   int distance)
{
    machEquivInfo equiv;

    equiv = machEquivSearch(table, name);
    if (!equiv) {
	if (table->count)
	    table->list = xrealloc(table->list, (table->count + 1)
				    * sizeof(*table->list));
	else
	    table->list = xmalloc(sizeof(*table->list));

	table->list[table->count].name = xstrdup(name);
	table->list[table->count++].score = distance;
    }
}

static void machCacheEntryVisit(machCache cache,
		machEquivTable table, const char * name, int distance)
{
    machCacheEntry entry;
    int i;

    entry = machCacheFindEntry(cache, name);
    if (!entry || entry->visited) return;

    entry->visited = 1;

    for (i = 0; i < entry->count; i++) {
	machAddEquiv(table, entry->equivs[i], distance);
    }

    for (i = 0; i < entry->count; i++) {
	machCacheEntryVisit(cache, table, entry->equivs[i], distance + 1);
    }
}

static void machFindEquivs(machCache cache, machEquivTable table,
		const char * key)
{
    int i;

    for (i = 0; i < cache->size; i++)
	cache->cache[i].visited = 0;

    while (table->count > 0) {
	--table->count;
	table->list[table->count].name = _free(table->list[table->count].name);
    }
    table->count = 0;
    table->list = _free(table->list);

    /*
     *	We have a general graph built using strings instead of pointers.
     *	Yuck. We have to start at a point at traverse it, remembering how
     *	far away everything is.
     */
   	/* FIX: table->list may be NULL. */
    machAddEquiv(table, key, 1);
    machCacheEntryVisit(cache, table, key, 2);
    return;
}

static rpmRC addCanon(canonEntry * table, int * tableLen, char * line,
		    const char * fn, int lineNum)
{
    canonEntry t;
    char *s, *s1;
    const char * tname;
    const char * tshort_name;
    int tnum;

    (*tableLen) += 2;
    *table = xrealloc(*table, sizeof(**table) * (*tableLen));

    t = & ((*table)[*tableLen - 2]);

    tname = strtok(line, ": \t");
    tshort_name = strtok(NULL, " \t");
    s = strtok(NULL, " \t");
    if (! (tname && tshort_name && s)) {
	rpmlog(RPMLOG_ERR, _("Incomplete data line at %s:%d\n"),
		fn, lineNum);
	return RPMRC_FAIL;
    }
    if (strtok(NULL, " \t")) {
	rpmlog(RPMLOG_ERR, _("Too many args in data line at %s:%d\n"),
	      fn, lineNum);
	return RPMRC_FAIL;
    }

   	/* LCL: s != NULL here. */
    tnum = strtoul(s, &s1, 10);
    if ((*s1) || (s1 == s) || (tnum == ULONG_MAX)) {
	rpmlog(RPMLOG_ERR, _("Bad arch/os number: %s (%s:%d)\n"), s,
	      fn, lineNum);
	return RPMRC_FAIL;
    }

    t[0].name = xstrdup(tname);
    t[0].short_name = (tshort_name ? xstrdup(tshort_name) : xstrdup(""));
    t[0].num = tnum;

    /* From A B C entry */
    /* Add  B B C entry */
    t[1].name = (tshort_name ? xstrdup(tshort_name) : xstrdup(""));
    t[1].short_name = (tshort_name ? xstrdup(tshort_name) : xstrdup(""));
    t[1].num = tnum;

    return RPMRC_OK;
}

static rpmRC addDefault(defaultEntry * table, int * tableLen, char * line,
			const char * fn, int lineNum)
{
    defaultEntry t;

    (*tableLen)++;
    *table = xrealloc(*table, sizeof(**table) * (*tableLen));

    t = & ((*table)[*tableLen - 1]);

    t->name = strtok(line, ": \t");
    t->defName = strtok(NULL, " \t");
    if (! (t->name && t->defName)) {
	rpmlog(RPMLOG_ERR, _("Incomplete default line at %s:%d\n"),
		 fn, lineNum);
	return RPMRC_FAIL;
    }
    if (strtok(NULL, " \t")) {
	rpmlog(RPMLOG_ERR, _("Too many args in default line at %s:%d\n"),
	      fn, lineNum);
	return RPMRC_FAIL;
    }

    t->name = xstrdup(t->name);
    t->defName = (t->defName ? xstrdup(t->defName) : NULL);

    return RPMRC_OK;
}

static canonEntry lookupInCanonTable(const char * name,
		const canonEntry table, int tableLen)
{
    while (tableLen) {
	tableLen--;
	if (strcmp(name, table[tableLen].name))
	    continue;
	return &(table[tableLen]);
    }

    return NULL;
}

static
const char * lookupInDefaultTable(const char * name,
		const defaultEntry table, int tableLen)
{
    while (tableLen) {
	tableLen--;
	if (table[tableLen].name && !strcmp(name, table[tableLen].name))
	    return table[tableLen].defName;
    }

    return name;
}

static void addMacroDefault(const char * macroname, const char * val,
		const char * body)
{
    if (body == NULL)
	body = val;
    addMacro(NULL, macroname, NULL, body, RMIL_DEFAULT);
}

static void setPathDefault(const char * macroname, const char * subdir)
{

    if (macroname != NULL) {
	char *body = rpmGetPath("%{_topdir}/", subdir, NULL);
	addMacro(NULL, macroname, NULL, body, RMIL_DEFAULT);
	free(body);
    }
}

static const char * const prescriptenviron = "\n\
RPM_SOURCE_DIR=\"%{_sourcedir}\"\n\
RPM_BUILD_DIR=\"%{_builddir}\"\n\
RPM_OPT_FLAGS=\"%{optflags}\"\n\
RPM_ARCH=\"%{_arch}\"\n\
RPM_OS=\"%{_os}\"\n\
export RPM_SOURCE_DIR RPM_BUILD_DIR RPM_OPT_FLAGS RPM_ARCH RPM_OS\n\
RPM_DOC_DIR=\"%{_docdir}\"\n\
export RPM_DOC_DIR\n\
RPM_PACKAGE_NAME=\"%{name}\"\n\
RPM_PACKAGE_VERSION=\"%{version}\"\n\
RPM_PACKAGE_RELEASE=\"%{release}\"\n\
export RPM_PACKAGE_NAME RPM_PACKAGE_VERSION RPM_PACKAGE_RELEASE\n\
%{?buildroot:RPM_BUILD_ROOT=\"%{buildroot}\"\n\
export RPM_BUILD_ROOT\n}\
";

static void setDefaults(void)
{

    addMacro(NULL, "_usr", NULL, "/usr", RMIL_DEFAULT);
    addMacro(NULL, "_var", NULL, LOCALSTATEDIR, RMIL_DEFAULT);

    addMacro(NULL, "_preScriptEnvironment",NULL, prescriptenviron,RMIL_DEFAULT);

    addMacroDefault("_topdir",
		"/usr/src/packages",		"%(echo $HOME)/rpmbuild");
    addMacroDefault("_tmppath",
		LOCALSTATEDIR "/tmp",		"%{_var}/tmp");
    addMacroDefault("_dbpath",
		LOCALSTATEDIR "/lib/rpm",		"%{_var}/lib/rpm");
    addMacroDefault("_defaultdocdir",
		"/usr/doc",		"%{_usr}/doc");

    addMacroDefault("_rpmfilename",
	"%%{ARCH}/%%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm",NULL);

    addMacroDefault("optflags",
		"-O2",			NULL);
    addMacroDefault("sigtype",
		"none",			NULL);
    addMacroDefault("_buildshell",
		"/bin/sh",		NULL);

    setPathDefault("_builddir",	"BUILD");
    setPathDefault("_buildrootdir",	"BUILDROOT");
    setPathDefault("_rpmdir",	"RPMS");
    setPathDefault("_srcrpmdir",	"SRPMS");
    setPathDefault("_sourcedir",	"SOURCES");
    setPathDefault("_specdir",	"SPECS");

}

/* FIX: se usage inconsistent, W2DO? */
static rpmRC doReadRC(const char * urlfn)
{
    char *s;
    char *se, *next, *buf = NULL, *fn;
    int linenum = 0;
    struct rpmOption searchOption, * option;
    rpmRC rc = RPMRC_FAIL;

    fn = rpmGetPath(urlfn, NULL);
    if (rpmioSlurp(fn, (uint8_t **) &buf, NULL) || buf == NULL) {
	goto exit;
    }
	
    next = buf;
    while (*next != '\0') {
	linenum++;

	s = se = next;

	/* Find end-of-line. */
	while (*se && *se != '\n') se++;
	if (*se != '\0') *se++ = '\0';
	next = se;

	/* Trim leading spaces */
	while (*s && risspace(*s)) s++;

	/* We used to allow comments to begin anywhere, but not anymore. */
	if (*s == '#' || *s == '\0') continue;

	/* Find end-of-keyword. */
	se = (char *)s;
	while (*se && !risspace(*se) && *se != ':') se++;

	if (risspace(*se)) {
	    *se++ = '\0';
	    while (*se && risspace(*se) && *se != ':') se++;
	}

	if (*se != ':') {
	    rpmlog(RPMLOG_ERR, _("missing ':' (found 0x%02x) at %s:%d\n"),
		     (unsigned)(0xff & *se), fn, linenum);
	    goto exit;
	}
	*se++ = '\0';	/* terminate keyword or option, point to value */
	while (*se && risspace(*se)) se++;

	/* Find keyword in table */
	searchOption.name = s;
	option = bsearch(&searchOption, optionTable, optionTableSize,
			 sizeof(optionTable[0]), optionCompare);

	if (option) {	/* For configuration variables  ... */
	    const char *arch, *val;

	    arch = val = NULL;
	    if (*se == '\0') {
		rpmlog(RPMLOG_ERR, _("missing argument for %s at %s:%d\n"),
		      option->name, fn, linenum);
		goto exit;
	    }

	    switch (option->var) {
	    case RPMVAR_INCLUDE:
		s = se;
		while (*se && !risspace(*se)) se++;
		if (*se != '\0') *se++ = '\0';

#if 0 /* XXX doesn't seem to do anything useful, only break things... */
		rpmRebuildTargetVars(NULL, NULL);
#endif

		if (doReadRC(s)) {
		    rpmlog(RPMLOG_ERR, _("cannot open %s at %s:%d: %m\n"),
			s, fn, linenum);
		    goto exit;
		} else {
		    continue;	/* XXX don't save include value as var/macro */
		}
	      	break;
	    default:
		break;
	    }

	    if (option->archSpecific) {
		arch = se;
		while (*se && !risspace(*se)) se++;
		if (*se == '\0') {
		    rpmlog(RPMLOG_ERR,
				_("missing architecture for %s at %s:%d\n"),
			  	option->name, fn, linenum);
		    goto exit;
		}
		*se++ = '\0';
		while (*se && risspace(*se)) se++;
		if (*se == '\0') {
		    rpmlog(RPMLOG_ERR,
				_("missing argument for %s at %s:%d\n"),
			  	option->name, fn, linenum);
		    goto exit;
		}
	    }
	
	    val = se;

	    /* Only add macros if appropriate for this arch */
	    if (option->macroize &&
	      (arch == NULL || !strcmp(arch, current[ARCH]))) {
		char *n, *name;
		n = name = xmalloc(strlen(option->name)+2);
		if (option->localize)
		    *n++ = '_';
		strcpy(n, option->name);
		addMacro(NULL, name, NULL, val, RMIL_RPMRC);
		free(name);
	    }
	    rpmSetVarArch(option->var, val, arch);
	    fn = _free(fn);

	} else {	/* For arch/os compatibilty tables ... */
	    int gotit;
	    int i;

	    gotit = 0;

	    for (i = 0; i < RPM_MACHTABLE_COUNT; i++) {
		if (!strncmp(tables[i].key, s, strlen(tables[i].key)))
		    break;
	    }

	    if (i < RPM_MACHTABLE_COUNT) {
		const char *rest = s + strlen(tables[i].key);
		if (*rest == '_') rest++;

		if (!strcmp(rest, "compat")) {
		    if (machCompatCacheAdd(se, fn, linenum,
						&tables[i].cache))
			goto exit;
		    gotit = 1;
		} else if (tables[i].hasTranslate &&
			   !strcmp(rest, "translate")) {
		    if (addDefault(&tables[i].defaults,
				   &tables[i].defaultsLength,
				   se, fn, linenum))
			goto exit;
		    gotit = 1;
		} else if (tables[i].hasCanon &&
			   !strcmp(rest, "canon")) {
		    if (addCanon(&tables[i].canons, &tables[i].canonsLength,
				 se, fn, linenum))
			goto exit;
		    gotit = 1;
		}
	    }

	    if (!gotit) {
		rpmlog(RPMLOG_ERR, _("bad option '%s' at %s:%d\n"),
			    s, fn, linenum);
		goto exit;
	    }
	}
    }
    rc = RPMRC_OK;

exit:
    free(fn);
    free(buf);

    return rc;
}


/**
 */
static rpmRC rpmPlatform(const char * platform)
{
    const char *cpu = NULL, *vendor = NULL, *os = NULL, *gnu = NULL;
    uint8_t * b = NULL;
    ssize_t blen = 0;
    int init_platform = 0;
    char * p, * pe;
    int rc;

    rc = rpmioSlurp(platform, &b, &blen);

    if (rc || b == NULL || blen <= 0) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    p = (char *)b;
    for (pe = p; p && *p; p = pe) {
	pe = strchr(p, '\n');
	if (pe)
	    *pe++ = '\0';

	while (*p && isspace(*p))
	    p++;
	if (*p == '\0' || *p == '#')
	    continue;

	if (init_platform) {
	    char * t = p + strlen(p);

	    while (--t > p && isspace(*t))
		*t = '\0';
	    if (t > p) {
		platpat = xrealloc(platpat, (nplatpat + 2) * sizeof(*platpat));
		platpat[nplatpat] = xstrdup(p);
		nplatpat++;
		platpat[nplatpat] = NULL;
	    }
	    continue;
	}

	cpu = p;
	vendor = "unknown";
	os = "unknown";
	gnu = NULL;
	while (*p && !(*p == '-' || isspace(*p)))
	    p++;
	if (*p != '\0') *p++ = '\0';

	vendor = p;
	while (*p && !(*p == '-' || isspace(*p)))
	    p++;
	if (*p != '-') {
	    if (*p != '\0') *p++ = '\0';
	    os = vendor;
	    vendor = "unknown";
	} else {
	    if (*p != '\0') *p++ = '\0';

	    os = p;
	    while (*p && !(*p == '-' || isspace(*p)))
		p++;
	    if (*p == '-') {
		*p++ = '\0';

		gnu = p;
		while (*p && !(*p == '-' || isspace(*p)))
		    p++;
	    }
	    if (*p != '\0') *p++ = '\0';
	}

	addMacro(NULL, "_host_cpu", NULL, cpu, -1);
	addMacro(NULL, "_host_vendor", NULL, vendor, -1);
	addMacro(NULL, "_host_os", NULL, os, -1);

	platpat = xrealloc(platpat, (nplatpat + 2) * sizeof(*platpat));
	platpat[nplatpat] = rpmExpand("%{_host_cpu}-%{_host_vendor}-%{_host_os}", (gnu && *gnu ? "-" : NULL), gnu, NULL);
	nplatpat++;
	platpat[nplatpat] = NULL;
	
	init_platform++;
    }
    rc = (init_platform ? RPMRC_OK : RPMRC_FAIL);

exit:
    b = _free(b);
    return rc;
}


#	if defined(__linux__) && defined(__i386__)
#include <setjmp.h>
#include <signal.h>

/*
 * Generic CPUID function
 */
static inline void cpuid(unsigned int op, unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx)
{
    asm volatile (
	"pushl	%%ebx		\n"
	"cpuid			\n"
	"movl	%%ebx,	%%esi	\n"
	"popl	%%ebx		\n"
    : "=a" (*eax), "=S" (*ebx), "=c" (*ecx), "=d" (*edx)
    : "a" (op));
}

/*
 * CPUID functions returning a single datum
 */
static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int tmp, val;
	cpuid(op, &val, &tmp, &tmp, &tmp);
	return val;
}

static inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int tmp, val;
	cpuid(op, &tmp, &val, &tmp, &tmp);
	return val;
}

static inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int tmp, val;
	cpuid(op, &tmp, &tmp, &val, &tmp);
	return val;
}

static inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int tmp, val;
	cpuid(op, &tmp, &tmp, &tmp, &val);
	return val;
}

static sigjmp_buf jenv;

static inline void model3(int _unused)
{
	siglongjmp(jenv, 1);
}

static inline int RPMClass(void)
{
	int cpu;
	unsigned int tfms, junk, cap, capamd;
	struct sigaction oldsa;
	
	sigaction(SIGILL, NULL, &oldsa);
	signal(SIGILL, model3);
	
	if (sigsetjmp(jenv, 1)) {
		sigaction(SIGILL, &oldsa, NULL);
		return 3;
	}
		
	if (cpuid_eax(0x000000000)==0) {
		sigaction(SIGILL, &oldsa, NULL);
		return 4;
	}

	cpuid(0x00000001, &tfms, &junk, &junk, &cap);
	cpuid(0x80000001, &junk, &junk, &junk, &capamd);
	
	cpu = (tfms>>8)&15;
	
	sigaction(SIGILL, &oldsa, NULL);

	if (cpu < 6)
		return cpu;
		
	if (cap & (1<<15)) {
		/* CMOV supported? */
		if (capamd & (1<<30))
			return 7;	/* 3DNOWEXT supported */
		return 6;
	}
		
	return 5;
}

/* should only be called for model 6 CPU's */
static int is_athlon(void)
{
	unsigned int eax, ebx, ecx, edx;
	char vendor[16];
	int i;
	
	cpuid (0, &eax, &ebx, &ecx, &edx);

 	/* If you care about space, you can just check ebx, ecx and edx directly
 	   instead of forming a string first and then doing a strcmp */
 	memset(vendor, 0, sizeof(vendor));
 	
 	for (i=0; i<4; i++)
 		vendor[i] = (unsigned char) (ebx >>(8*i));
 	for (i=0; i<4; i++)
 		vendor[4+i] = (unsigned char) (edx >>(8*i));
 	for (i=0; i<4; i++)
 		vendor[8+i] = (unsigned char) (ecx >>(8*i));
 		
 	if (strncmp(vendor, "AuthenticAMD", 12) != 0)  
 		return 0;

	return 1;
}

static int is_pentium3()
{
    unsigned int eax, ebx, ecx, edx, family, model;
    char vendor[16];
    cpuid(0, &eax, &ebx, &ecx, &edx);
    memset(vendor, 0, sizeof(vendor));
    *((unsigned int *)&vendor[0]) = ebx;
    *((unsigned int *)&vendor[4]) = edx;
    *((unsigned int *)&vendor[8]) = ecx;
    if (strncmp(vendor, "GenuineIntel", 12) != 0)
	return 0;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    family = (eax >> 8) & 0x0f;
    model = (eax >> 4) & 0x0f;
    if (family == 6)
	switch (model)
	{
	    case 7:	// Pentium III, Pentium III Xeon (model 7)
	    case 8:	// Pentium III, Pentium III Xeon, Celeron (model 8)
	    case 9:	// Pentium M
			/*
			    Intel recently announced its new technology for mobile platforms,
			    named Centrino, and presents it as a big advance in mobile PCs.
			    One of the main part of Centrino consists in a brand new CPU,
			    the Pentium M, codenamed Banias, that we'll study in this review.
			    A particularity of this CPU is that it was designed for mobile platform
			    exclusively, unlike previous mobile CPU (Pentium III-M, Pentium 4-M)
			    that share the same micro-architecture as their desktop counterparts.
			    The Pentium M introduces a new micro-architecture, adapted for mobility
			    constraints, and that is halfway between the Pentium III and the Pentium 4.
						    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			*/
	    case 10:	// Pentium III Xeon (model A)
	    case 11:	// Pentium III (model B)
		return 1;
	}
    return 0;
}

static int is_pentium4()
{
    unsigned int eax, ebx, ecx, edx, family, model;
    char vendor[16];
    cpuid(0, &eax, &ebx, &ecx, &edx);
    memset(vendor, 0, sizeof(vendor));
    *((unsigned int *)&vendor[0]) = ebx;
    *((unsigned int *)&vendor[4]) = edx;
    *((unsigned int *)&vendor[8]) = ecx;
    if (strncmp(vendor, "GenuineIntel", 12) != 0)
	return 0;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    family = (eax >> 8) & 0x0f;
    model = (eax >> 4) & 0x0f;
    if (family == 15)
	switch (model)
	{
	    case 0:	// Pentium 4, Pentium 4 Xeon                 (0.18um)
	    case 1:	// Pentium 4, Pentium 4 Xeon MP, Celeron     (0.18um)
	    case 2:	// Pentium 4, Mobile Pentium 4-M,
			// Pentium 4 Xeon, Pentium 4 Xeon MP,
			// Celeron, Mobile Celron                    (0.13um)
	    case 3:	// Pentium 4, Celeron                        (0.09um)
		return 1;
	}
    return 0;
}

static int is_geode()
{
    unsigned int eax, ebx, ecx, edx, family, model;
    char vendor[16];
    /* If you care about space, you can just check ebx, ecx and edx directly
       instead of forming a string first and then doing a strcmp */
    memset(vendor, 0, sizeof(vendor));
    
    cpuid(0, &eax, &ebx, &ecx, &edx);
    memset(vendor, 0, sizeof(vendor));
    *((unsigned int *)&vendor[0]) = ebx;
    *((unsigned int *)&vendor[4]) = edx;
    *((unsigned int *)&vendor[8]) = ecx;
    if (strncmp(vendor, "AuthenticAMD", 12) != 0)  
        return 0;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    family = (eax >> 8) & 0x0f;
    model = (eax >> 4) & 0x0f;
    if (family == 5)
	switch (model)
	{
            case 10: // Geode 
                return 1;
        }
    return 0;
}
#endif

#if defined(__linux__) && defined(__powerpc__)
static jmp_buf mfspr_jmpbuf;

static void mfspr_ill(int notused)
{
    longjmp(mfspr_jmpbuf, -1);
}
#endif

/**
 */
static void defaultMachine(const char ** arch,
		const char ** os)
{
    static struct utsname un;
    static int gotDefaults = 0;
    char * chptr;
    canonEntry canon;
    int rc;

    while (!gotDefaults) {
	if (!rpmPlatform(platform)) {
	    char * s;
	    s = rpmExpand("%{_host_cpu}", NULL);
	    if (s) {
		rstrlcpy(un.machine, s, sizeof(un.machine));
		s = _free(s);
	    }
	    s = rpmExpand("%{_host_os}", NULL);
	    if (s) {
		rstrlcpy(un.sysname, s, sizeof(un.sysname));
		s = _free(s);
	    }
	    gotDefaults = 1;
	    break;
	}
	rc = uname(&un);
	if (rc < 0) return;

#if !defined(__linux__)
#ifdef SNI
	/* USUALLY un.sysname on sinix does start with the word "SINIX"
	 * let's be absolutely sure
	 */
	strncpy(un.sysname, "SINIX", sizeof(un.sysname));
#endif
	if (!strcmp(un.sysname, "AIX")) {
	    strcpy(un.machine, __power_pc() ? "ppc" : "rs6000");
	    sprintf(un.sysname,"aix%s.%s", un.version, un.release);
	}
	else if(!strcmp(un.sysname, "Darwin")) { 
#ifdef __ppc__
	    strcpy(un.machine, "ppc");
#else ifdef __i386__
	    strcpy(un.machine, "i386");
#endif 
	}
	else if (!strcmp(un.sysname, "SunOS")) {
	    if (!strncmp(un.release,"4", 1)) /* SunOS 4.x */ {
		int fd;
		for (fd = 0;
		    (un.release[fd] != 0 && (fd < sizeof(un.release)));
		    fd++) {
		      if (!risdigit(un.release[fd]) && (un.release[fd] != '.')) {
			un.release[fd] = 0;
			break;
		      }
		    }
		    sprintf(un.sysname,"sunos%s",un.release);
	    }

	    else /* Solaris 2.x: n.x.x becomes n-3.x.x */
		sprintf(un.sysname, "solaris%1d%s", atoi(un.release)-3,
			un.release+1+(atoi(un.release)/10));

	    /* Solaris on Intel hardware reports i86pc instead of i386
	     * (at least on 2.6 and 2.8)
	     */
	    if (!strcmp(un.machine, "i86pc"))
		sprintf(un.machine, "i386");
	}
	else if (!strcmp(un.sysname, "HP-UX"))
	    /*make un.sysname look like hpux9.05 for example*/
	    sprintf(un.sysname, "hpux%s", strpbrk(un.release, "123456789"));
	else if (!strcmp(un.sysname, "OSF1"))
	    /*make un.sysname look like osf3.2 for example*/
	    sprintf(un.sysname, "osf%s", strpbrk(un.release, "123456789"));
	else if (!strncmp(un.sysname, "IP", 2))
	    un.sysname[2] = '\0';
	else if (!strncmp(un.sysname, "SINIX", 5)) {
	    sprintf(un.sysname, "sinix%s",un.release);
	    if (!strncmp(un.machine, "RM", 2))
		sprintf(un.machine, "mips");
	}
	else if ((!strncmp(un.machine, "34", 2) ||
		!strncmp(un.machine, "33", 2)) && \
		!strncmp(un.release, "4.0", 3))
	{
	    /* we are on ncr-sysv4 */
	    char * prelid = NULL;
	    FD_t fd = Fopen("/etc/.relid", "r.fdio");
	    int gotit = 0;
	    if (fd != NULL && !Ferror(fd)) {
		chptr = xcalloc(1, 256);
		{   int irelid = Fread(chptr, sizeof(*chptr), 256, fd);
		    (void) Fclose(fd);
		    /* example: "112393 RELEASE 020200 Version 01 OS" */
		    if (irelid > 0) {
			if ((prelid = strstr(chptr, "RELEASE "))){
			    prelid += strlen("RELEASE ")+1;
			    sprintf(un.sysname,"ncr-sysv4.%.*s",1,prelid);
			    gotit = 1;
			}
		    }
		}
		chptr = _free (chptr);
	    }
	    if (!gotit)	/* parsing /etc/.relid file failed? */
		strcpy(un.sysname,"ncr-sysv4");
	    /* wrong, just for now, find out how to look for i586 later*/
	    strcpy(un.machine,"i486");
	}
#endif	/* __linux__ */

	/* get rid of the hyphens in the sysname */
	for (chptr = un.machine; *chptr != '\0'; chptr++)
	    if (*chptr == '/') *chptr = '-';

#	if defined(__MIPSEL__) || defined(__MIPSEL) || defined(_MIPSEL)
	    /* little endian */
	    strcpy(un.machine, "mipsel");
#	elif defined(__MIPSEB__) || defined(__MIPSEB) || defined(_MIPSEB)
	   /* big endian */
		strcpy(un.machine, "mips");
#	endif

#	if defined(__hpux) && defined(_SC_CPU_VERSION)
	{
#	    if !defined(CPU_PA_RISC1_2)
#                define CPU_PA_RISC1_2  0x211 /* HP PA-RISC1.2 */
#           endif
#           if !defined(CPU_PA_RISC2_0)
#               define CPU_PA_RISC2_0  0x214 /* HP PA-RISC2.0 */
#           endif
	    int cpu_version = sysconf(_SC_CPU_VERSION);

#	    if defined(CPU_HP_MC68020)
		if (cpu_version == CPU_HP_MC68020)
		    strcpy(un.machine, "m68k");
#	    endif
#	    if defined(CPU_HP_MC68030)
		if (cpu_version == CPU_HP_MC68030)
		    strcpy(un.machine, "m68k");
#	    endif
#	    if defined(CPU_HP_MC68040)
		if (cpu_version == CPU_HP_MC68040)
		    strcpy(un.machine, "m68k");
#	    endif

#	    if defined(CPU_PA_RISC1_0)
		if (cpu_version == CPU_PA_RISC1_0)
		    strcpy(un.machine, "hppa1.0");
#	    endif
#	    if defined(CPU_PA_RISC1_1)
		if (cpu_version == CPU_PA_RISC1_1)
		    strcpy(un.machine, "hppa1.1");
#	    endif
#	    if defined(CPU_PA_RISC1_2)
		if (cpu_version == CPU_PA_RISC1_2)
		    strcpy(un.machine, "hppa1.2");
#	    endif
#	    if defined(CPU_PA_RISC2_0)
		if (cpu_version == CPU_PA_RISC2_0)
		    strcpy(un.machine, "hppa2.0");
#	    endif
	}
#	endif	/* hpux */

#	if defined(__linux__) && defined(__sparc__)
	if (!strcmp(un.machine, "sparc")) {
	    #define PERS_LINUX		0x00000000
	    #define PERS_LINUX_32BIT	0x00800000
	    #define PERS_LINUX32	0x00000008

	    extern int personality(unsigned long);
	    int oldpers;
	    
	    oldpers = personality(PERS_LINUX_32BIT);
	    if (oldpers != -1) {
		if (personality(PERS_LINUX) != -1) {
		    uname(&un);
		    if (! strcmp(un.machine, "sparc64")) {
			strcpy(un.machine, "sparcv9");
			oldpers = PERS_LINUX32;
		    }
		}
		personality(oldpers);
	    }
	}
#	endif	/* sparc*-linux */

#	if defined(__GNUC__) && defined(__alpha__)
	{
	    unsigned long amask, implver;
	    register long v0 __asm__("$0") = -1;
	    __asm__ (".long 0x47e00c20" : "=r"(v0) : "0"(v0));
	    amask = ~v0;
	    __asm__ (".long 0x47e03d80" : "=r"(v0));
	    implver = v0;
	    switch (implver) {
	    case 1:
	    	switch (amask) {
	    	case 0: strcpy(un.machine, "alphaev5"); break;
	    	case 1: strcpy(un.machine, "alphaev56"); break;
	    	case 0x101: strcpy(un.machine, "alphapca56"); break;
	    	}
	    	break;
	    case 2:
	    	switch (amask) {
	    	case 0x303: strcpy(un.machine, "alphaev6"); break;
	    	case 0x307: strcpy(un.machine, "alphaev67"); break;
	    	}
	    	break;
	    }
	}
#	endif

#	if defined(__linux__) && defined(__i386__)
	{
	    char class = (char) (RPMClass() | '0');

	    if ((class == '6' && is_athlon()) || class == '7')
	    	strcpy(un.machine, "athlon");
	    else if (is_pentium4())
		strcpy(un.machine, "pentium4");
	    else if (is_pentium3())
		strcpy(un.machine, "pentium3");
	    else if (is_geode())
		strcpy(un.machine, "geode");
	    else if (strchr("3456", un.machine[1]) && un.machine[1] != class)
		un.machine[1] = class;
	}
#	endif

	/* the uname() result goes through the arch_canon table */
	canon = lookupInCanonTable(un.machine,
				   tables[RPM_MACHTABLE_INSTARCH].canons,
				   tables[RPM_MACHTABLE_INSTARCH].canonsLength);
	if (canon)
	    rstrlcpy(un.machine, canon->short_name, sizeof(un.machine));

	canon = lookupInCanonTable(un.sysname,
				   tables[RPM_MACHTABLE_INSTOS].canons,
				   tables[RPM_MACHTABLE_INSTOS].canonsLength);
	if (canon)
	    rstrlcpy(un.sysname, canon->short_name, sizeof(un.sysname));
	gotDefaults = 1;
	break;
    }

    if (arch) *arch = un.machine;
    if (os) *os = un.sysname;
}

static
const char * rpmGetVarArch(int var, const char * arch)
{
    const struct rpmvarValue * next;

    if (arch == NULL) arch = current[ARCH];

    if (arch) {
	next = &values[var];
	while (next) {
	    if (next->arch && !strcmp(next->arch, arch)) return next->value;
	    next = next->next;
	}
    }

    next = values + var;
    while (next && next->arch) next = next->next;

    return next ? next->value : NULL;
}

static const char *rpmGetVar(int var)
{
    return rpmGetVarArch(var, NULL);
}

static void rpmSetVarArch(int var, const char * val, const char * arch)
{
    struct rpmvarValue * next = values + var;

    if (next->value) {
	if (arch) {
	    while (next->next) {
		if (next->arch && !strcmp(next->arch, arch)) break;
		next = next->next;
	    }
	} else {
	    while (next->next) {
		if (!next->arch) break;
		next = next->next;
	    }
	}

	if (next->arch && arch && !strcmp(next->arch, arch)) {
	    next->value = _free(next->value);
	    next->arch = _free(next->arch);
	} else if (next->arch || arch) {
	    next->next = xmalloc(sizeof(*next->next));
	    next = next->next;
	    next->value = NULL;
	    next->arch = NULL;
	    next->next = NULL;
	}
    }

    next->value = _free(next->value);
    next->value = xstrdup(val);
    next->arch = (arch ? xstrdup(arch) : NULL);
}

void rpmSetTables(int archTable, int osTable)
{
    const char * arch, * os;

    defaultMachine(&arch, &os);

    if (currTables[ARCH] != archTable) {
	currTables[ARCH] = archTable;
	rebuildCompatTables(ARCH, arch);
    }

    if (currTables[OS] != osTable) {
	currTables[OS] = osTable;
	rebuildCompatTables(OS, os);
    }
}

int rpmMachineScore(int type, const char * name)
{
    machEquivInfo info = machEquivSearch(&tables[type].equiv, name);
    return (info != NULL ? info->score : 0);
}

/** \ingroup rpmrc
 * Set current arch/os names.
 * NULL as argument is set to the default value (munged uname())
 * pushed through a translation table (if appropriate).
 * @deprecated Use addMacro to set _target_* macros.
 * @todo Eliminate 
 *
 * @param arch          arch name (or NULL)
 * @param os            os name (or NULL)
 *          */

static void rpmSetMachine(const char * arch, const char * os)
{
    const char * host_cpu, * host_os;

    defaultMachine(&host_cpu, &host_os);

    if (arch == NULL) {
	arch = host_cpu;
	if (tables[currTables[ARCH]].hasTranslate)
	    arch = lookupInDefaultTable(arch,
			    tables[currTables[ARCH]].defaults,
			    tables[currTables[ARCH]].defaultsLength);
    }
    if (arch == NULL) return;	/* XXX can't happen */

    if (os == NULL) {
	os = host_os;
	if (tables[currTables[OS]].hasTranslate)
	    os = lookupInDefaultTable(os,
			    tables[currTables[OS]].defaults,
			    tables[currTables[OS]].defaultsLength);
    }
    if (os == NULL) return;	/* XXX can't happen */

    if (!current[ARCH] || strcmp(arch, current[ARCH])) {
	current[ARCH] = _free(current[ARCH]);
	current[ARCH] = xstrdup(arch);
	rebuildCompatTables(ARCH, host_cpu);
    }

    if (!current[OS] || strcmp(os, current[OS])) {
	char * t = xstrdup(os);
	current[OS] = _free(current[OS]);
	/*
	 * XXX Capitalizing the 'L' is needed to insure that old
	 * XXX os-from-uname (e.g. "Linux") is compatible with the new
	 * XXX os-from-platform (e.g "linux" from "sparc-*-linux").
	 * XXX A copy of this string is embedded in headers and is
	 * XXX used by rpmInstallPackage->{os,arch}Okay->rpmMachineScore->
	 * XXX to verify correct arch/os from headers.
	 */
	if (!strcmp(t, "linux"))
	    *t = 'L';
	current[OS] = t;
	
	rebuildCompatTables(OS, host_os);
    }
}

static void rebuildCompatTables(int type, const char * name)
{
    machFindEquivs(&tables[currTables[type]].cache,
		   &tables[currTables[type]].equiv,
		   name);
}

static void getMachineInfo(int type, const char ** name,
			int * num)
{
    canonEntry canon;
    int which = currTables[type];

    /* use the normal canon tables, even if we're looking up build stuff */
    if (which >= 2) which -= 2;

    canon = lookupInCanonTable(current[type],
			       tables[which].canons,
			       tables[which].canonsLength);

    if (canon) {
	if (num) *num = canon->num;
	if (name) *name = canon->short_name;
    } else {
	if (num) *num = 255;
	if (name) *name = current[type];

	if (tables[currTables[type]].hasCanon) {
	    rpmlog(RPMLOG_WARNING, _("Unknown system: %s\n"), current[type]);
	    rpmlog(RPMLOG_WARNING, _("Please contact %s\n"), PACKAGE_BUGREPORT);
	}
    }
}

void rpmGetArchInfo(const char ** name, int * num)
{
    getMachineInfo(ARCH, name, num);
}

void rpmGetOsInfo(const char ** name, int * num)
{
    getMachineInfo(OS, name, num);
}

static void rpmRebuildTargetVars(const char ** target, const char ** canontarget)
{

    char *ca = NULL, *co = NULL, *ct = NULL;
    int x;

    /* Rebuild the compat table to recalculate the current target arch.  */

    rpmSetMachine(NULL, NULL);
    rpmSetTables(RPM_MACHTABLE_INSTARCH, RPM_MACHTABLE_INSTOS);
    rpmSetTables(RPM_MACHTABLE_BUILDARCH, RPM_MACHTABLE_BUILDOS);

    if (target && *target) {
	char *c;
	/* Set arch and os from specified build target */
	ca = xstrdup(*target);
	if ((c = strchr(ca, '-')) != NULL) {
	    *c++ = '\0';
	    
	    if ((co = strrchr(c, '-')) == NULL) {
		co = c;
	    } else {
		if (!rstrcasecmp(co, "-gnu"))
		    *co = '\0';
		if ((co = strrchr(c, '-')) == NULL)
		    co = c;
		else
		    co++;
	    }
	    if (co != NULL) co = xstrdup(co);
	}
    } else {
	const char *a = NULL;
	const char *o = NULL;
	/* Set build target from rpm arch and os */
	rpmGetArchInfo(&a, NULL);
	ca = (a) ? xstrdup(a) : NULL;
	rpmGetOsInfo(&o, NULL);
	co = (o) ? xstrdup(o) : NULL;
    }

    /* If still not set, Set target arch/os from default uname(2) values */
    if (ca == NULL) {
	const char *a = NULL;
	defaultMachine(&a, NULL);
	ca = (a) ? xstrdup(a) : NULL;
    }
    for (x = 0; ca[x] != '\0'; x++)
	ca[x] = rtolower(ca[x]);

    if (co == NULL) {
	const char *o = NULL;
	defaultMachine(NULL, &o);
	co = (o) ? xstrdup(o) : NULL;
    }
    for (x = 0; co[x] != '\0'; x++)
	co[x] = rtolower(co[x]);

    /* XXX For now, set canonical target to arch-os */
    if (ct == NULL) {
	ct = xmalloc(strlen(ca) + sizeof("-") + strlen(co));
	sprintf(ct, "%s-%s", ca, co);
    }

/*
 * XXX All this macro pokery/jiggery could be achieved by doing a delayed
 *	rpmInitMacros(NULL, PER-PLATFORM-MACRO-FILE-NAMES);
 */
    delMacro(NULL, "_target");
    addMacro(NULL, "_target", NULL, ct, RMIL_RPMRC);
    delMacro(NULL, "_target_cpu");
    addMacro(NULL, "_target_cpu", NULL, ca, RMIL_RPMRC);
    delMacro(NULL, "_target_os");
    addMacro(NULL, "_target_os", NULL, co, RMIL_RPMRC);
/*
 * XXX Make sure that per-arch optflags is initialized correctly.
 */
  { const char *optflags = rpmGetVarArch(RPMVAR_OPTFLAGS, ca);
    if (optflags != NULL) {
	delMacro(NULL, "optflags");
	addMacro(NULL, "optflags", NULL, optflags, RMIL_RPMRC);
    }
  }

    if (canontarget)
	*canontarget = ct;
    else
	ct = _free(ct);
    ca = _free(ca);
    co = _free(co);
}

void rpmFreeRpmrc(void)
{
    int i, j, k;

    if (platpat)
    for (i = 0; i < nplatpat; i++)
	platpat[i] = _free(platpat[i]);
    platpat = _free(platpat);
    nplatpat = 0;

    for (i = 0; i < RPM_MACHTABLE_COUNT; i++) {
	tableType t;
	t = tables + i;
	if (t->equiv.list) {
	    for (j = 0; j < t->equiv.count; j++)
		t->equiv.list[j].name = _free(t->equiv.list[j].name);
	    t->equiv.list = _free(t->equiv.list);
	    t->equiv.count = 0;
	}
	if (t->cache.cache) {
	    for (j = 0; j < t->cache.size; j++) {
		machCacheEntry e;
		e = t->cache.cache + j;
		if (e == NULL)
		    continue;
		e->name = _free(e->name);
		if (e->equivs) {
		    for (k = 0; k < e->count; k++)
			e->equivs[k] = _free(e->equivs[k]);
		    e->equivs = _free(e->equivs);
		}
	    }
	    t->cache.cache = _free(t->cache.cache);
	    t->cache.size = 0;
	}
	if (t->defaults) {
	    for (j = 0; j < t->defaultsLength; j++) {
		t->defaults[j].name = _free(t->defaults[j].name);
		t->defaults[j].defName = _free(t->defaults[j].defName);
	    }
	    t->defaults = _free(t->defaults);
	    t->defaultsLength = 0;
	}
	if (t->canons) {
	    for (j = 0; j < t->canonsLength; j++) {
		t->canons[j].name = _free(t->canons[j].name);
		t->canons[j].short_name = _free(t->canons[j].short_name);
	    }
	    t->canons = _free(t->canons);
	    t->canonsLength = 0;
	}
    }

    for (i = 0; i < RPMVAR_NUM; i++) {
	struct rpmvarValue * vp;
	while ((vp = values[i].next) != NULL) {
	    values[i].next = vp->next;
	    vp->value = _free(vp->value);
	    vp->arch = _free(vp->arch);
	    vp = _free(vp);
	}
	values[i].value = _free(values[i].value);
	values[i].arch = _free(values[i].arch);
    }
    current[OS] = _free(current[OS]);
    current[ARCH] = _free(current[ARCH]);
    defaultsInitialized = 0;
/* FIX: platpat/current may be NULL */

    /* XXX doesn't really belong here but... */
    rpmFreeCrypto();

    return;
}

/** \ingroup rpmrc
 * Read rpmrc (and macro) configuration file(s).
 * @param rcfiles	colon separated files to read (NULL uses default)
 * @return		RPMRC_OK on success
 */
static rpmRC rpmReadRC(const char * rcfiles)
{
    ARGV_t p, globs = NULL, files = NULL;
    rpmRC rc = RPMRC_FAIL;

    if (!defaultsInitialized) {
	setDefaults();
	defaultsInitialized = 1;
    }

    if (rcfiles == NULL)
	rcfiles = defrcfiles;

    /* Expand any globs in rcfiles. Missing files are ok here. */
    argvSplit(&globs, rcfiles, ":");
    for (p = globs; *p; p++) {
	ARGV_t av = NULL;
	if (rpmGlob(*p, NULL, &av) == 0) {
	    argvAppend(&files, av);
	    argvFree(av);
	}
    }
    argvFree(globs);

    /* Read each file in rcfiles. */
    for (p = files; p && *p; p++) {
	/* XXX Only /usr/lib/rpm/rpmrc must exist in default rcfiles list */
	if (access(*p, R_OK) != 0) {
	    if (rcfiles == defrcfiles && p != files)
		continue;
	    rpmlog(RPMLOG_ERR, _("Unable to open %s for reading: %m.\n"), *p);
	    goto exit;
	    break;
	} else {
	    rc = doReadRC(*p);
	}
    }
    rc = RPMRC_OK;
    rpmSetMachine(NULL, NULL);	/* XXX WTFO? Why bother? */

exit:
    argvFree(files);
    return rc;
}

int rpmReadConfigFiles(const char * file, const char * target)
{
    mode_t mode = 0022;
    /* Reset umask to its default umask(2) value. */
    mode = umask(mode);

    /* Force preloading of name service libraries in case we go chrooting */
    (void) gethostbyname("localhost");

    /* Preset target macros */
   	/* FIX: target can be NULL */
    rpmRebuildTargetVars(&target, NULL);

    /* Read the files */
    if (rpmReadRC(file)) return -1;

    if (macrofiles != NULL) {
	char *mf = rpmGetPath(macrofiles, NULL);
	rpmInitMacros(NULL, mf);
	_free(mf);
    }

    /* Reset target macros */
    rpmRebuildTargetVars(&target, NULL);

    /* Finally set target platform */
    {	char *cpu = rpmExpand("%{_target_cpu}", NULL);
	char *os = rpmExpand("%{_target_os}", NULL);
	rpmSetMachine(cpu, os);
	cpu = _free(cpu);
	os = _free(os);
    }

    /* Force Lua state initialization */
#ifdef WITH_LUA
    (void)rpmluaGetPrintBuffer(NULL);
#endif

    return 0;
}

int rpmShowRC(FILE * fp)
{
    const struct rpmOption *opt;
    rpmds ds = NULL;
    int i, xx;
    machEquivTable equivTable;

    /* the caller may set the build arch which should be printed here */
    fprintf(fp, "ARCHITECTURE AND OS:\n");
    fprintf(fp, "build arch            : %s\n", current[ARCH]);

    fprintf(fp, "compatible build archs:");
    equivTable = &tables[RPM_MACHTABLE_BUILDARCH].equiv;
    for (i = 0; i < equivTable->count; i++)
	fprintf(fp," %s", equivTable->list[i].name);
    fprintf(fp, "\n");

    fprintf(fp, "build os              : %s\n", current[OS]);

    fprintf(fp, "compatible build os's :");
    equivTable = &tables[RPM_MACHTABLE_BUILDOS].equiv;
    for (i = 0; i < equivTable->count; i++)
	fprintf(fp," %s", equivTable->list[i].name);
    fprintf(fp, "\n");

    rpmSetTables(RPM_MACHTABLE_INSTARCH, RPM_MACHTABLE_INSTOS);
    rpmSetMachine(NULL, NULL);	/* XXX WTFO? Why bother? */

    fprintf(fp, "install arch          : %s\n", current[ARCH]);
    fprintf(fp, "install os            : %s\n", current[OS]);

    fprintf(fp, "compatible archs      :");
    equivTable = &tables[RPM_MACHTABLE_INSTARCH].equiv;
    for (i = 0; i < equivTable->count; i++)
	fprintf(fp," %s", equivTable->list[i].name);
    fprintf(fp, "\n");

    fprintf(fp, "compatible os's       :");
    equivTable = &tables[RPM_MACHTABLE_INSTOS].equiv;
    for (i = 0; i < equivTable->count; i++)
	fprintf(fp," %s", equivTable->list[i].name);
    fprintf(fp, "\n");

    fprintf(fp, "\nRPMRC VALUES:\n");
    for (i = 0, opt = optionTable; i < optionTableSize; i++, opt++) {
	const char *s = rpmGetVar(opt->var);
	if (s != NULL || rpmIsVerbose())
	    fprintf(fp, "%-21s : %s\n", opt->name, s ? s : "(not set)");
    }
    fprintf(fp, "\n");

    fprintf(fp, "Features supported by rpmlib:\n");
    xx = rpmdsRpmlib(&ds, NULL);
    ds = rpmdsInit(ds);
    while (rpmdsNext(ds) >= 0) {
        const char * DNEVR = rpmdsDNEVR(ds);
        if (DNEVR != NULL)
            fprintf(fp, "    %s\n", DNEVR+2);
    }
    ds = rpmdsFree(ds);
    fprintf(fp, "\n");

    rpmDumpMacroTable(NULL, fp);

    return 0;
}
