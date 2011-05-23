#include "system.h"

#include <pwd.h>
#include <grp.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmstring.h>

#include "lib/misc.h"
#include "lib/rpmug.h"
#include "debug.h"

#define HASHTYPE strCache
#define HTKEYTYPE const char *
#include "lib/rpmhash.H"
#include "lib/rpmhash.C"
#undef HASHTYPE
#undef HTKEYTYPE

static strCache strStash = NULL;

const char * rpmugStashStr(const char *str)
{
    const char *ret = NULL;
    if (str) {
	if (strStash == NULL) {
	    strStash = strCacheCreate(64, hashFunctionString, strcmp,
				      (strCacheFreeKey)rfree);
	}
	
	if (!strCacheGetEntry(strStash, str, &ret)) {
	    strCacheAddEntry(strStash, xstrdup(str));
	    (void) strCacheGetEntry(strStash, str, &ret);
	}
    }
    return ret;
}

#if defined(__GLIBC__)

static int inchroot;

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
#endif

/* 
 * These really ought to use hash tables. I just made the
 * guess that most files would be owned by root or the same person/group
 * who owned the last file. Those two values are cached, everything else
 * is looked up via getpw() and getgr() functions.  If this performs
 * too poorly I'll have to implement it properly :-(
 */

int rpmugUid(const char * thisUname, uid_t * uid)
{
    static char * lastUname = NULL;
    static size_t lastUnameLen = 0;
    static size_t lastUnameAlloced;
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

    thisUnameLen = strlen(thisUname);
    if (lastUname == NULL || thisUnameLen != lastUnameLen ||
	!rstreq(thisUname, lastUname))
    {
	if (lastUnameAlloced < thisUnameLen + 1) {
	    lastUnameAlloced = thisUnameLen + 10;
	    lastUname = xrealloc(lastUname, lastUnameAlloced);	/* XXX memory leak */
	}

#if defined(__GLIBC__)
	if (inchroot) {
	    int uid =  safe_lookup("/etc/passwd", thisUname);
	    if (uid < 0)
		return -1;
	    lastUid = uid;
	} else
#endif
	{
	    pwent = getpwnam(thisUname);
	    if (pwent == NULL) {
		/* FIX: shrug */
		endpwent();
		pwent = getpwnam(thisUname);
		if (pwent == NULL) return -1;
	    }
	    lastUid = pwent->pw_uid;
	}

	strcpy(lastUname, thisUname);
	lastUnameLen = thisUnameLen;
    }

    *uid = lastUid;

    return 0;
}

int rpmugGid(const char * thisGname, gid_t * gid)
{
    static char * lastGname = NULL;
    static size_t lastGnameLen = 0;
    static size_t lastGnameAlloced;
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

    thisGnameLen = strlen(thisGname);
    if (lastGname == NULL || thisGnameLen != lastGnameLen ||
	!rstreq(thisGname, lastGname))
    {
	if (lastGnameAlloced < thisGnameLen + 1) {
	    lastGnameAlloced = thisGnameLen + 10;
	    lastGname = xrealloc(lastGname, lastGnameAlloced);	/* XXX memory leak */
	}

#if defined(__GLIBC__)
	if (inchroot) {
	    int gid =  safe_lookup("/etc/group", thisGname);
	    if (gid < 0)
		return -1;
	    lastGid = gid;
	} else
#endif
	{
	    grent = getgrnam(thisGname);
	    if (grent == NULL) {
		/* FIX: shrug */
		endgrent();
		grent = getgrnam(thisGname);
		if (grent == NULL) {
		    return -1;
		}
	    }
	    lastGid = grent->gr_gid;
	}
	strcpy(lastGname, thisGname);
	lastGnameLen = thisGnameLen;
    }

    *gid = lastGid;

    return 0;
}

const char * rpmugUname(uid_t uid)
{
    static uid_t lastUid = (uid_t) -1;
    static char * lastUname = NULL;
    static size_t lastUnameAlloced = 0;

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
	if (lastUnameAlloced < len + 1) {
	    lastUnameAlloced = len + 20;
	    lastUname = xrealloc(lastUname, lastUnameAlloced);
	}
	strcpy(lastUname, pwent->pw_name);

	return lastUname;
    }
}

const char * rpmugGname(gid_t gid)
{
    static gid_t lastGid = (gid_t) -1;
    static char * lastGname = NULL;
    static size_t lastGnameAlloced = 0;

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
	if (lastGnameAlloced < len + 1) {
	    lastGnameAlloced = len + 20;
	    lastGname = xrealloc(lastGname, lastGnameAlloced);
	}
	strcpy(lastGname, grent->gr_name);

	return lastGname;
    }
}

void rpmugFree(void)
{
    rpmugUid(NULL, NULL);
    rpmugGid(NULL, NULL);
    rpmugUname(-1);
    rpmugGname(-1);
    strStash = strCacheFree(strStash);
}

void rpmugChroot(int in)
{
    /* tell libc to drop caches / file descriptors */
    endpwent();
    endgrent();
    /* drop our own caches */
    rpmugUid(NULL, NULL);
    rpmugGid(NULL, NULL);
#if defined(__GLIBC__)
    inchroot = in;
#endif
}
