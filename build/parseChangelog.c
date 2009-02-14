/** \ingroup rpmbuild
 * \file build/parseChangelog.c
 *  Parse %changelog section from spec file.
 */

#include "system.h"

#include <rpm/header.h>
#include <rpm/rpmbuild.h>
#include <rpm/rpmlog.h>
#include "debug.h"

#define SKIPSPACE(s) { while (*(s) && risspace(*(s))) (s)++; }
#define SKIPNONSPACE(s) { while (*(s) && !risspace(*(s))) (s)++; }

void addChangelogEntry(Header h, time_t time, const char *name, const char *text)
{
    rpm_time_t mytime = time;	/* XXX convert to header representation */
				
    headerPutUint32(h, RPMTAG_CHANGELOGTIME, &mytime, 1);
    headerPutString(h, RPMTAG_CHANGELOGNAME, name);
    headerPutString(h, RPMTAG_CHANGELOGTEXT, text);
}

/**
 * Parse date string to seconds.
 * @param datestr	date string (e.g. 'Wed Jan 1 1997')
 * @retval secs		secs since the unix epoch
 * @return 		0 on success, -1 on error
 */
static int dateToTimet(const char * datestr, time_t * secs)
{
    int rc = -1; /* assume failure */
    struct tm time;
    const char * const * idx;
    char *p, *pe, *q, *date;
    
    static const char * const days[] =
	{ "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL };
    static const char * const months[] =
	{ "Jan", "Feb", "Mar", "Apr", "May", "Jun",
 	  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };
    static const char const lengths[] =
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    memset(&time, 0, sizeof(time));

    date = xstrdup(datestr);
    pe = date;

    /* day of week */
    p = pe; SKIPSPACE(p);
    if (*p == '\0') goto exit;
    pe = p; SKIPNONSPACE(pe); if (*pe != '\0') *pe++ = '\0';
    for (idx = days; *idx && strcmp(*idx, p); idx++)
	{};
    if (*idx == NULL) goto exit;

    /* month */
    p = pe; SKIPSPACE(p);
    if (*p == '\0') goto exit;
    pe = p; SKIPNONSPACE(pe); if (*pe != '\0') *pe++ = '\0';
    for (idx = months; *idx && strcmp(*idx, p); idx++)
	{};
    if (*idx == NULL) goto exit;
    time.tm_mon = idx - months;

    /* day */
    p = pe; SKIPSPACE(p);
    if (*p == '\0') goto exit;
    pe = p; SKIPNONSPACE(pe); if (*pe != '\0') *pe++ = '\0';

    /* make this noon so the day is always right (as we make this UTC) */
    time.tm_hour = 12;

    time.tm_mday = strtol(p, &q, 10);
    if (!(q && *q == '\0')) goto exit;
    if (time.tm_mday < 0 || time.tm_mday > lengths[time.tm_mon]) goto exit;

    /* year */
    p = pe; SKIPSPACE(p);
    if (*p == '\0') goto exit;
    pe = p; SKIPNONSPACE(pe); if (*pe != '\0') *pe++ = '\0';
    time.tm_year = strtol(p, &q, 10);
    if (!(q && *q == '\0')) goto exit;
    if (time.tm_year < 1990 || time.tm_year >= 3000) goto exit;
    time.tm_year -= 1900;

    *secs = mktime(&time);
    if (*secs == -1) goto exit;

    /* adjust to GMT */
    *secs += timezone;
    rc = 0;

exit:
    free(date);
    return rc;
}

/**
 * Add %changelog section to header.
 * @param h		header
 * @param sb		changelog strings
 * @return		RPMRC_OK on success
 */
static rpmRC addChangelog(Header h, StringBuf sb)
{
    char *s;
    int i;
    time_t time;
    time_t lastTime = 0;
    char *date, *name, *text, *next;

    s = getStringBuf(sb);

    /* skip space */
    SKIPSPACE(s);

    while (*s != '\0') {
	if (*s != '*') {
	    rpmlog(RPMLOG_ERR,
			_("%%changelog entries must start with *\n"));
	    return RPMRC_FAIL;
	}

	/* find end of line */
	date = s;
	while(*s && *s != '\n') s++;
	if (! *s) {
	    rpmlog(RPMLOG_ERR, _("incomplete %%changelog entry\n"));
	    return RPMRC_FAIL;
	}
	*s = '\0';
	text = s + 1;
	
	/* 4 fields of date */
	date++;
	s = date;
	for (i = 0; i < 4; i++) {
	    SKIPSPACE(s);
	    SKIPNONSPACE(s);
	}
	SKIPSPACE(date);
	if (dateToTimet(date, &time)) {
	    rpmlog(RPMLOG_ERR, _("bad date in %%changelog: %s\n"), date);
	    return RPMRC_FAIL;
	}
	if (lastTime && lastTime < time) {
	    rpmlog(RPMLOG_ERR,
		     _("%%changelog not in descending chronological order\n"));
	    return RPMRC_FAIL;
	}
	lastTime = time;

	/* skip space to the name */
	SKIPSPACE(s);
	if (! *s) {
	    rpmlog(RPMLOG_ERR, _("missing name in %%changelog\n"));
	    return RPMRC_FAIL;
	}

	/* name */
	name = s;
	while (*s != '\0') s++;
	while (s > name && risspace(*s)) {
	    *s-- = '\0';
	}
	if (s == name) {
	    rpmlog(RPMLOG_ERR, _("missing name in %%changelog\n"));
	    return RPMRC_FAIL;
	}

	/* text */
	SKIPSPACE(text);
	if (! *text) {
	    rpmlog(RPMLOG_ERR, _("no description in %%changelog\n"));
	    return RPMRC_FAIL;
	}
	    
	/* find the next leading '*' (or eos) */
	s = text;
	do {
	   s++;
	} while (*s && (*(s-1) != '\n' || *s != '*'));
	next = s;
	s--;

	/* backup to end of description */
	while ((s > text) && risspace(*s)) {
	    *s-- = '\0';
	}
	
	addChangelogEntry(h, time, name, text);
	s = next;
    }

    return RPMRC_OK;
}

int parseChangelog(rpmSpec spec)
{
    int nextPart, rc, res = PART_ERROR;
    StringBuf sb = newStringBuf();
    int argc;
    const char ** argv = NULL;

    if ((rc = poptParseArgvString(spec->line, &argc, &argv))) {
	rpmlog(RPMLOG_ERR, _("line %d: Error parsing %%changelog: %s\n"),
		 spec->lineNum, poptStrerror(rc));
	goto exit;
    }

    if (argc>1){
    	if ((argc>2)&&(!strcmp(argv[1],"-f"))){
	    if (*argv[2] == '/') {
		spec->packages->changeFile= rpmGetPath(argv[2], NULL);
	    } else {
		/* XXX FIXME: add %{buildsubdir} */
		spec->packages->changeFile = rpmGetPath("%{_builddir}/",
			(spec->buildSubdir ? spec->buildSubdir : "") ,
			"/", argv[2], NULL);
	    }
	} else{
	    rpmlog(RPMLOG_ERR, _("line %d: Error parsing %%changelog: incorrect options.\n"),
		 spec->lineNum);
	    goto exit;
	}
    }

    if ((rc = readLine(spec, STRIP_COMMENTS)) > 0) {
       res = PART_NONE;
       goto exit;
    } else if (rc < 0) {
       goto exit;
    }
	
    while (! (nextPart = isPart(spec->line))) {
	appendStringBuf(sb, spec->line);
	if ((rc = readLine(spec, STRIP_COMMENTS)) > 0) {
	    nextPart = PART_NONE;
	    break;
	} else if (rc < 0) {
	    goto exit;
	}
    }

    if (addChangelog(spec->packages->header, sb)) {
	goto exit;
    }
    res = nextPart;

exit:
    sb = freeStringBuf(sb);

    return res;
}

int processChangelog(rpmSpec spec)
{
	StringBuf sb = newStringBuf();
	FILE *fd;
	int res;
	char buf[BUFSIZ];
	if (spec->packages->changeFile == NULL)
		return RPMRC_OK;
		

	/* XXX W2DO? urlPath might be useful here. */
	fd = fopen(spec->packages->changeFile, "r");

	if (fd == NULL || ferror(fd)) {
	    rpmlog(RPMLOG_ERR, _("Could not open %%changelog file %s: %m\n"), 
	    	spec->packages->changeFile);
	    return RPMRC_FAIL;
	}
	
	rpmlog(RPMLOG_DEBUG, "\tprocessing changelog from %s\n", 
		spec->packages->changeFile);

	while (fgets(buf, sizeof(buf), fd)) {
	    appendStringBuf(sb, buf);
	}
	(void) fclose(fd);

	res = addChangelog(spec->packages->header, sb);
	sb = freeStringBuf(sb);
	
	return res;
}

