/** \ingroup rpmbuild
 * \file build/parseSpec.c
 *  Top level dispatcher for spec file parsing.
 */

#include "system.h"

#include <rpm/rpmtypes.h>
#include <rpm/rpmlib.h>		/* RPM_MACHTABLE & related */
#include <rpm/rpmbuild.h>
#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmfileutil.h>
#include "debug.h"

#define SKIPSPACE(s) { while (*(s) && risspace(*(s))) (s)++; }
#define SKIPNONSPACE(s) { while (*(s) && !risspace(*(s))) (s)++; }

/* XXX FIXME: strlen for these is calculated at runtime, preventing const */
static struct PartRec {
    int part;
    size_t len;
    const char * token;
} partList[] = {
    { PART_PREAMBLE,      0, "%package"},
    { PART_PREP,          0, "%prep"},
    { PART_BUILD,         0, "%build"},
    { PART_INSTALL,       0, "%install"},
    { PART_CHECK,         0, "%check"},
    { PART_CLEAN,         0, "%clean"},
    { PART_PREUN,         0, "%preun"},
    { PART_POSTUN,        0, "%postun"},
    { PART_PRETRANS,      0, "%pretrans"},
    { PART_POSTTRANS,     0, "%posttrans"},
    { PART_PRE,           0, "%pre"},
    { PART_POST,          0, "%post"},
    { PART_FILES,         0, "%files"},
    { PART_CHANGELOG,     0, "%changelog"},
    { PART_DESCRIPTION,   0, "%description"},
    { PART_TRIGGERPOSTUN, 0, "%triggerpostun"},
    { PART_TRIGGERPREIN,  0, "%triggerprein"},
    { PART_TRIGGERUN,     0, "%triggerun"},
    { PART_TRIGGERIN,     0, "%triggerin"},
    { PART_TRIGGERIN,     0, "%trigger"},
    { PART_VERIFYSCRIPT,  0, "%verifyscript"},
    {0, 0, 0}
};

/**
 */
static inline void initParts(struct PartRec *p)
{
    for (; p->token != NULL; p++)
	p->len = strlen(p->token);
}

rpmParseState isPart(const char *line)
{
    struct PartRec *p;

    if (partList[0].len == 0)
	initParts(partList);
    
    for (p = partList; p->token != NULL; p++) {
	char c;
	if (rstrncasecmp(line, p->token, p->len))
	    continue;
	c = *(line + p->len);
	if (c == '\0' || risspace(c))
	    break;
    }

    return (p->token ? p->part : PART_NONE);
}

/**
 */
static int matchTok(const char *token, const char *line)
{
    const char *b, *be = line;
    size_t toklen = strlen(token);
    int rc = 0;

    while ( *(b = be) != '\0' ) {
	SKIPSPACE(b);
	be = b;
	SKIPNONSPACE(be);
	if (be == b)
	    break;
	if (toklen != (be-b) || rstrncasecmp(token, b, (be-b)))
	    continue;
	rc = 1;
	break;
    }

    return rc;
}

void handleComments(char *s)
{
    SKIPSPACE(s);
    if (*s == '#')
	*s = '\0';
}

/**
 */
static void forceIncludeFile(rpmSpec spec, const char * fileName)
{
    OFI_t * ofi;

    ofi = newOpenFileInfo();
    ofi->fileName = xstrdup(fileName);
    ofi->next = spec->fileStack;
    spec->fileStack = ofi;
}

static int restoreFirstChar(rpmSpec spec)
{
    /* Restore 1st char in (possible) next line */
    if (spec->nextline != NULL && spec->nextpeekc != '\0') {
	*spec->nextline = spec->nextpeekc;
	spec->nextpeekc = '\0';
	return 1;
    }
    return 0;
}

/* Return zero on success, 1 if we need to read more and -1 on errors. */
static int copyNextLineFromOFI(rpmSpec spec, OFI_t *ofi)
{
    char ch;

    /* Expand next line from file into line buffer */
    if (!(spec->nextline && *spec->nextline)) {
	int pc = 0, bc = 0, nc = 0;
	char *from, *to, *p;
	to = spec->lbufPtr ? spec->lbufPtr : spec->lbuf;
	from = ofi->readPtr;
	ch = ' ';
	while (*from && ch != '\n')
	    ch = *to++ = *from++;
	spec->lbufPtr = to;
	*to++ = '\0';
	ofi->readPtr = from;

	/* Check if we need another line before expanding the buffer. */
	for (p = spec->lbuf; *p; p++) {
	    switch (*p) {
		case '\\':
		    switch (*(p+1)) {
			case '\n': p++, nc = 1; break;
			case '\0': break;
			default: p++; break;
		    }
		    break;
		case '\n': nc = 0; break;
		case '%':
		    switch (*(p+1)) {
			case '{': p++, bc++; break;
			case '(': p++, pc++; break;
			case '%': p++; break;
		    }
		    break;
		case '{': if (bc > 0) bc++; break;
		case '}': if (bc > 0) bc--; break;
		case '(': if (pc > 0) pc++; break;
		case ')': if (pc > 0) pc--; break;
	    }
	}
	
	/* If it doesn't, ask for one more line. */
	if (pc || bc || nc ) {
	    spec->nextline = "";
	    return 1;
	}
	spec->lbufPtr = spec->lbuf;

	/* Don't expand macros (eg. %define) in false branch of %if clause */
	if (spec->readStack->reading &&
	    expandMacros(spec, spec->macros, spec->lbuf, sizeof(spec->lbuf))) {
		rpmlog(RPMLOG_ERR, _("line %d: %s\n"),
			spec->lineNum, spec->lbuf);
		return -1;
	}
	spec->nextline = spec->lbuf;
    }
    return 0;
}

static void copyNextLineFinish(rpmSpec spec, int strip)
{
    char *last;
    char ch;

    /* Find next line in expanded line buffer */
    spec->line = last = spec->nextline;
    ch = ' ';
    while (*spec->nextline && ch != '\n') {
	ch = *spec->nextline++;
	if (!risspace(ch))
	    last = spec->nextline;
    }

    /* Save 1st char of next line in order to terminate current line. */
    if (*spec->nextline != '\0') {
	spec->nextpeekc = *spec->nextline;
	*spec->nextline = '\0';
    }
    
    if (strip & STRIP_COMMENTS)
	handleComments(spec->line);
    
    if (strip & STRIP_TRAILINGSPACE)
	*last = '\0';
}

static int readLineFromOFI(rpmSpec spec, OFI_t *ofi)
{
retry:
    /* Make sure the current file is open */
    if (ofi->fd == NULL) {
	ofi->fd = Fopen(ofi->fileName, "r.fpio");
	if (ofi->fd == NULL || Ferror(ofi->fd)) {
	    /* XXX Fstrerror */
	    rpmlog(RPMLOG_ERR, _("Unable to open %s: %s\n"),
		     ofi->fileName, Fstrerror(ofi->fd));
	    return PART_ERROR;
	}
	spec->lineNum = ofi->lineNum = 0;
    }

    /* Make sure we have something in the read buffer */
    if (!(ofi->readPtr && *(ofi->readPtr))) {
	FILE * f = fdGetFILE(ofi->fd);
	if (f == NULL || !fgets(ofi->readBuf, BUFSIZ, f)) {
	    /* EOF */
	    if (spec->readStack->next) {
		rpmlog(RPMLOG_ERR, _("Unclosed %%if\n"));
	        return PART_ERROR;
	    }

	    /* remove this file from the stack */
	    spec->fileStack = ofi->next;
	    (void) Fclose(ofi->fd);
	    ofi->fileName = _free(ofi->fileName);
	    ofi = _free(ofi);

	    /* only on last file do we signal EOF to caller */
	    ofi = spec->fileStack;
	    if (ofi == NULL)
		return 1;

	    /* otherwise, go back and try the read again. */
	    goto retry;
	}
	ofi->readPtr = ofi->readBuf;
	ofi->lineNum++;
	spec->lineNum = ofi->lineNum;
	if (spec->sl) {
	    speclines sl = spec->sl;
	    if (sl->sl_nlines == sl->sl_nalloc) {
		sl->sl_nalloc += 100;
		sl->sl_lines = (char **) xrealloc(sl->sl_lines, 
			sl->sl_nalloc * sizeof(*(sl->sl_lines)));
	    }
	    sl->sl_lines[sl->sl_nlines++] = xstrdup(ofi->readBuf);
	}
    }
    return 0;
}

int readLine(rpmSpec spec, int strip)
{
    char  *s;
    int match;
    struct ReadLevelEntry *rl;
    OFI_t *ofi = spec->fileStack;
    int rc;

    if (!restoreFirstChar(spec)) {
    retry:
	if ((rc = readLineFromOFI(spec, ofi)) != 0)
	    return rc;

	/* Copy next file line into the spec line buffer */
	rc = copyNextLineFromOFI(spec, ofi);
	if (rc > 0) {
	    goto retry;
	} else if (rc < 0) {
	    return PART_ERROR;
	}
    }

    copyNextLineFinish(spec, strip);

    s = spec->line;
    SKIPSPACE(s);

    match = -1;
    if (!spec->readStack->reading && !strncmp("%if", s, sizeof("%if")-1)) {
	match = 0;
    } else if (! strncmp("%ifarch", s, sizeof("%ifarch")-1)) {
	char *arch = rpmExpand("%{_target_cpu}", NULL);
	s += 7;
	match = matchTok(arch, s);
	arch = _free(arch);
    } else if (! strncmp("%ifnarch", s, sizeof("%ifnarch")-1)) {
	char *arch = rpmExpand("%{_target_cpu}", NULL);
	s += 8;
	match = !matchTok(arch, s);
	arch = _free(arch);
    } else if (! strncmp("%ifos", s, sizeof("%ifos")-1)) {
	char *os = rpmExpand("%{_target_os}", NULL);
	s += 5;
	match = matchTok(os, s);
	os = _free(os);
    } else if (! strncmp("%ifnos", s, sizeof("%ifnos")-1)) {
	char *os = rpmExpand("%{_target_os}", NULL);
	s += 6;
	match = !matchTok(os, s);
	os = _free(os);
    } else if (! strncmp("%if", s, sizeof("%if")-1)) {
	s += 3;
        match = parseExpressionBoolean(spec, s);
	if (match < 0) {
	    rpmlog(RPMLOG_ERR,
			_("%s:%d: parseExpressionBoolean returns %d\n"),
			ofi->fileName, ofi->lineNum, match);
	    return PART_ERROR;
	}
    } else if (! strncmp("%else", s, sizeof("%else")-1)) {
	s += 5;
	if (! spec->readStack->next) {
	    /* Got an else with no %if ! */
	    rpmlog(RPMLOG_ERR,
			_("%s:%d: Got a %%else with no %%if\n"),
			ofi->fileName, ofi->lineNum);
	    return PART_ERROR;
	}
	spec->readStack->reading =
	    spec->readStack->next->reading && ! spec->readStack->reading;
	spec->line[0] = '\0';
    } else if (! strncmp("%endif", s, sizeof("%endif")-1)) {
	s += 6;
	if (! spec->readStack->next) {
	    /* Got an end with no %if ! */
	    rpmlog(RPMLOG_ERR,
			_("%s:%d: Got a %%endif with no %%if\n"),
			ofi->fileName, ofi->lineNum);
	    return PART_ERROR;
	}
	rl = spec->readStack;
	spec->readStack = spec->readStack->next;
	free(rl);
	spec->line[0] = '\0';
    } else if (! strncmp("%include", s, sizeof("%include")-1)) {
	char *fileName, *endFileName, *p;

	s += 8;
	fileName = s;
	if (! risspace(*fileName)) {
	    rpmlog(RPMLOG_ERR, _("malformed %%include statement\n"));
	    return PART_ERROR;
	}
	SKIPSPACE(fileName);
	endFileName = fileName;
	SKIPNONSPACE(endFileName);
	p = endFileName;
	SKIPSPACE(p);
	if (*p != '\0') {
	    rpmlog(RPMLOG_ERR, _("malformed %%include statement\n"));
	    return PART_ERROR;
	}
	*endFileName = '\0';

	forceIncludeFile(spec, fileName);

	ofi = spec->fileStack;
	goto retry;
    }

    if (match != -1) {
	rl = xmalloc(sizeof(*rl));
	rl->reading = spec->readStack->reading && match;
	rl->next = spec->readStack;
	spec->readStack = rl;
	spec->line[0] = '\0';
    }

    if (! spec->readStack->reading) {
	spec->line[0] = '\0';
    }

    /* FIX: spec->readStack->next should be dependent */
    return 0;
}

void closeSpec(rpmSpec spec)
{
    OFI_t *ofi;

    while (spec->fileStack) {
	ofi = spec->fileStack;
	spec->fileStack = spec->fileStack->next;
	if (ofi->fd) (void) Fclose(ofi->fd);
	ofi->fileName = _free(ofi->fileName);
	ofi = _free(ofi);
    }
}

extern int noLang;		/* XXX FIXME: pass as arg */

int parseSpec(rpmts ts, const char *specFile, const char *rootDir,
		const char *buildRoot, int recursing, const char *passPhrase,
		const char *cookie, int anyarch, int force)
{
    rpmParseState parsePart = PART_PREAMBLE;
    int initialPackage = 1;
    Package pkg;
    rpmSpec spec;
    
    /* Set up a new Spec structure with no packages. */
    spec = newSpec();

    spec->specFile = rpmGetPath(specFile, NULL);
    spec->fileStack = newOpenFileInfo();
    spec->fileStack->fileName = xstrdup(spec->specFile);
    /* If buildRoot not specified, use default %{buildroot} */
    if (buildRoot) {
	spec->buildRoot = xstrdup(buildRoot);
    } else {
	spec->buildRoot = rpmGetPath("%{?buildroot:%{buildroot}}", NULL);
    }
    addMacro(NULL, "_docdir", NULL, "%{_defaultdocdir}", RMIL_SPEC);
    spec->recursing = recursing;
    spec->anyarch = anyarch;
    spec->force = force;

    if (rootDir)
	spec->rootDir = xstrdup(rootDir);
    if (passPhrase)
	spec->passPhrase = xstrdup(passPhrase);
    if (cookie)
	spec->cookie = xstrdup(cookie);

    spec->timeCheck = rpmExpandNumeric("%{_timecheck}");

    /* All the parse*() functions expect to have a line pre-read */
    /* in the spec's line buffer.  Except for parsePreamble(),   */
    /* which handles the initial entry into a spec file.         */
    
   	/* LCL: parsePart is modified @*/
    while (parsePart != PART_NONE) {
	int goterror = 0;
	switch (parsePart) {
	/* XXX Trap unexpected RPMRC_FAIL returns for now */
	case RPMRC_FAIL:
	    rpmlog(RPMLOG_ERR, "FIXME: got RPMRC_FAIL from spec parse\n");
	    abort();
	case PART_ERROR: /* fallthrough */
	default:
	    goterror = 1;
	    break;
	case PART_PREAMBLE:
	    parsePart = parsePreamble(spec, initialPackage);
	    initialPackage = 0;
	    break;
	case PART_PREP:
	    parsePart = parsePrep(spec);
	    break;
	case PART_BUILD:
	case PART_INSTALL:
	case PART_CHECK:
	case PART_CLEAN:
	    parsePart = parseBuildInstallClean(spec, parsePart);
	    break;
	case PART_CHANGELOG:
	    parsePart = parseChangelog(spec);
	    break;
	case PART_DESCRIPTION:
	    parsePart = parseDescription(spec);
	    break;

	case PART_PRE:
	case PART_POST:
	case PART_PREUN:
	case PART_POSTUN:
	case PART_PRETRANS:
	case PART_POSTTRANS:
	case PART_VERIFYSCRIPT:
	case PART_TRIGGERPREIN:
	case PART_TRIGGERIN:
	case PART_TRIGGERUN:
	case PART_TRIGGERPOSTUN:
	    parsePart = parseScript(spec, parsePart);
	    break;

	case PART_FILES:
	    parsePart = parseFiles(spec);
	    break;

	case PART_NONE:		/* XXX avoid gcc whining */
	case PART_LAST:
	case PART_BUILDARCHITECTURES:
	    break;
	}

	if (goterror || parsePart >= PART_LAST) {
	    goto errxit;
	}

	if (parsePart == PART_BUILDARCHITECTURES) {
	    int index;
	    int x;

	    closeSpec(spec);

	    /* LCL: sizeof(spec->BASpecs[0]) -nullderef whine here */
	    spec->BASpecs = xcalloc(spec->BACount, sizeof(*spec->BASpecs));
	    index = 0;
	    if (spec->BANames != NULL)
	    for (x = 0; x < spec->BACount; x++) {

		/* Skip if not arch is not compatible. */
		if (!rpmMachineScore(RPM_MACHTABLE_BUILDARCH, spec->BANames[x]))
		    continue;
		addMacro(NULL, "_target_cpu", NULL, spec->BANames[x], RMIL_RPMRC);
		spec->BASpecs[index] = NULL;
		if (parseSpec(ts, specFile, spec->rootDir, buildRoot, 1,
				  passPhrase, cookie, anyarch, force)
		 || (spec->BASpecs[index] = rpmtsSetSpec(ts, NULL)) == NULL)
		{
			spec->BACount = index;
			goto errxit;
		}
		delMacro(NULL, "_target_cpu");
		index++;
	    }

	    spec->BACount = index;
	    if (! index) {
		rpmlog(RPMLOG_ERR,
			_("No compatible architectures found for build\n"));
		goto errxit;
	    }

	    /*
	     * Return the 1st child's fully parsed Spec structure.
	     * The restart of the parse when encountering BuildArch
	     * causes problems for "rpm -q --specfile". This is
	     * still a hack because there may be more than 1 arch
	     * specified (unlikely but possible.) There's also the
	     * further problem that the macro context, particularly
	     * %{_target_cpu}, disagrees with the info in the header.
	     */
	    if (spec->BACount >= 1) {
		rpmSpec nspec = spec->BASpecs[0];
		spec->BASpecs = _free(spec->BASpecs);
		spec = freeSpec(spec);
		spec = nspec;
	    }

	    (void) rpmtsSetSpec(ts, spec);
	    return 0;
	}
    }
   	/* LCL: parsePart is modified @*/

    /* Check for description in each package and add arch and os */
  {
    char *platform = rpmExpand("%{_target_platform}", NULL);
    char *arch = rpmExpand("%{_target_cpu}", NULL);
    char *os = rpmExpand("%{_target_os}", NULL);

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	if (!headerIsEntry(pkg->header, RPMTAG_DESCRIPTION)) {
	    const char * name;
	    (void) headerNVR(pkg->header, &name, NULL, NULL);
	    rpmlog(RPMLOG_ERR, _("Package has no %%description: %s\n"), name);
	    goto errxit;
	}

	headerPutString(pkg->header, RPMTAG_OS, os);
	/* noarch subpackages already have arch set here, leave it alone */
	if (!headerIsEntry(pkg->header, RPMTAG_ARCH)) {
	    headerPutString(pkg->header, RPMTAG_ARCH, arch);
	}
	headerPutString(pkg->header, RPMTAG_PLATFORM, platform);

	pkg->ds = rpmdsThis(pkg->header, RPMTAG_REQUIRENAME, RPMSENSE_EQUAL);

    }

    platform = _free(platform);
    arch = _free(arch);
    os = _free(os);
  }

    closeSpec(spec);
    (void) rpmtsSetSpec(ts, spec);

    return 0;

errxit:
    spec = freeSpec(spec);
    return PART_ERROR;
}
