/**
 * \file lib/misc.c
 */

#include "system.h"

/* just to put a marker in librpm.a */
const char * const RPMVERSION = VERSION;

#include <rpm/rpmlog.h>
#include <rpm/rpmstring.h>

#include "lib/misc.h"

#include "debug.h"

/*
 * Unfortunatelly glibc caches nss/nscd data and there is no
 * good way to flush those caches when we did a chroot(). Thus
 * we need to parse /etc/passwd and /etc/group ourselfs.
 */
static int safe_lookup(const char * file, const char * name)
{
    FILE *fp;
    int l;
    char buf[4096], *p;

    if (!name || !*name)
	return -1;
    l = strlen(name);
    if ((fp = fopen(file, "r")) == 0)
	return -1;
    while ((p = fgets(buf, sizeof(buf), fp)) != 0) {
	if (*p == '#')
	    continue;
	while (*p && (*p == ' ' || *p == '\t'))
	    p++;
	if (strncmp(p, name, l) != 0 || p[l] != ':')
	    continue;
	p = strchr(p + l + 1, ':');
	if (!p)
	    continue;
	fclose(fp);
	p++;
	while (*p && (*p == ' ' || *p == '\t'))
	    p++;
	return atoi(p);
    }
    fclose(fp);
    return -1;
}

/* unameToUid(), uidTouname() and the group variants are really poorly
   implemented. They really ought to use hash tables. I just made the
   guess that most files would be owned by root or the same person/group
   who owned the last file. Those two values are cached, everything else
   is looked up via getpw() and getgr() functions.  If this performs
   too poorly I'll have to implement it properly :-( */

int unameToUid_safe(const char * thisUname, uid_t * uid, int safe)
{
static char * lastUname = NULL;
    static size_t lastUnameLen = 0;
    static size_t lastUnameAlloced;
    static int lastUnameSafe;
    static uid_t lastUid;
    struct passwd * pwent;
    size_t thisUnameLen;

    if (!thisUname) {
	lastUnameLen = 0;
	return -1;
    } else if (rstreq(thisUname, "root")) {
	*uid = 0;
	return 0;
    }

    if (safe != lastUnameSafe) {
	lastUnameLen = 0;
	lastUnameSafe = safe;
    }

    thisUnameLen = strlen(thisUname);
    if (lastUname == NULL || thisUnameLen != lastUnameLen ||
	!rstreq(thisUname, lastUname))
    {
	if (lastUnameAlloced < thisUnameLen + 1) {
	    lastUnameAlloced = thisUnameLen + 10;
	    lastUname = xrealloc(lastUname, lastUnameAlloced);	/* XXX memory leak */
	}
	strcpy(lastUname, thisUname);

	if (safe) {
	    int uid = safe_lookup("/etc/passwd", thisUname);
	    if (uid < 0)
		return -1;
	    lastUid = (uid_t)uid;
	} else {
	    pwent = getpwnam(thisUname);
	    if (pwent == NULL) {
		/* FIX: shrug */
		endpwent();
		pwent = getpwnam(thisUname);
		if (pwent == NULL) return -1;
	    }
	    lastUid = pwent->pw_uid;
	}
    }

    *uid = lastUid;

    return 0;
}

int unameToUid(const char * thisUname, uid_t * uid)
{
    return unameToUid_safe(thisUname, uid, 0);
}


int gnameToGid_safe(const char * thisGname, gid_t * gid, int safe)
{
static char * lastGname = NULL;
    static size_t lastGnameLen = 0;
    static size_t lastGnameAlloced;
    static int lastGnameSafe;
    static gid_t lastGid;
    size_t thisGnameLen;
    struct group * grent;

    if (thisGname == NULL) {
	lastGnameLen = 0;
	return -1;
    } else if (rstreq(thisGname, "root")) {
	*gid = 0;
	return 0;
    }

    if (safe != lastGnameSafe) {
	lastGnameLen = 0;
	lastGnameSafe = safe;
    }

    thisGnameLen = strlen(thisGname);
    if (lastGname == NULL || thisGnameLen != lastGnameLen ||
	!rstreq(thisGname, lastGname))
    {
	if (lastGnameAlloced < thisGnameLen + 1) {
	    lastGnameAlloced = thisGnameLen + 10;
	    lastGname = xrealloc(lastGname, lastGnameAlloced);	/* XXX memory leak */
	}
	strcpy(lastGname, thisGname);

	if (safe) {
	    int gid = safe_lookup("/etc/group", thisGname);
	    if (gid < 0)
		return -1;
	    lastGid = (gid_t)gid;
	} else {
	    grent = getgrnam(thisGname);
	    if (grent == NULL) {
		/* FIX: shrug */
		endgrent();
		grent = getgrnam(thisGname);
		if (grent == NULL) {
#ifdef STRANGE_FEDORA_HACKS
		    /* XXX The filesystem package needs group/lock w/o getgrnam. */
		    if (rstreq(thisGname, "lock")) {
			*gid = lastGid = 54;
			return 0;
		    } else
		    if (rstreq(thisGname, "mail")) {
			*gid = lastGid = 12;
			return 0;
		    } else
#endif
		    return -1;
		}
	    }
	    lastGid = grent->gr_gid;
	}
    }

    *gid = lastGid;

    return 0;
}

int gnameToGid(const char * thisGname, gid_t * gid)
{
    return gnameToGid_safe(thisGname, gid, 0);
}


const char * uidToUname(uid_t uid)
{
    static uid_t lastUid = (uid_t) -1;
static char * lastUname = NULL;
    static size_t lastUnameLen = 0;

    if (uid == (uid_t) -1) {
	lastUid = (uid_t) -1;
	return NULL;
    } else if (uid == (uid_t) 0) {
	return "root";
    } else if (uid == lastUid) {
	return lastUname;
    } else {
	struct passwd * pwent = getpwuid(uid);
	size_t len;

	if (pwent == NULL) return NULL;

	lastUid = uid;
	len = strlen(pwent->pw_name);
	if (lastUnameLen < len + 1) {
	    lastUnameLen = len + 20;
	    lastUname = xrealloc(lastUname, lastUnameLen);
	}
	strcpy(lastUname, pwent->pw_name);

	return lastUname;
    }
}

const char * gidToGname(gid_t gid)
{
    static gid_t lastGid = (gid_t) -1;
static char * lastGname = NULL;
    static size_t lastGnameLen = 0;

    if (gid == (gid_t) -1) {
	lastGid = (gid_t) -1;
	return NULL;
    } else if (gid == (gid_t) 0) {
	return "root";
    } else if (gid == lastGid) {
	return lastGname;
    } else {
	struct group * grent = getgrgid(gid);
	size_t len;

	if (grent == NULL) return NULL;

	lastGid = gid;
	len = strlen(grent->gr_name);
	if (lastGnameLen < len + 1) {
	    lastGnameLen = len + 20;
	    lastGname = xrealloc(lastGname, lastGnameLen);
	}
	strcpy(lastGname, grent->gr_name);

	return lastGname;
    }
}
