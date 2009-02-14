/**
 * \file system.h
 */

#ifndef	H_SYSTEM
#define	H_SYSTEM

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include <sys/types.h>

#include <sys/stat.h>
#include <stdio.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

/* <unistd.h> should be included before any preprocessor test
   of _POSIX_VERSION.  */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#if !defined(__GLIBC__)
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char ** environ;
#endif /* __APPLE__ */
#endif
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if NEED_TIMEZONE
extern time_t timezone;
#endif

/* Since major is a function on SVR4, we can't use `ifndef major'.  */
#if MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#define HAVE_MAJOR
#endif
#if MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#define HAVE_MAJOR
#endif
#ifdef major			/* Might be defined in sys/types.h.  */
#define HAVE_MAJOR
#endif

#ifndef HAVE_MAJOR
#define major(dev)  (((dev) >> 8) & 0xff)
#define minor(dev)  ((dev) & 0xff)
#define makedev(maj, min)  (((maj) << 8) | (min))
#endif
#undef HAVE_MAJOR

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#else
# include <strings.h>
char *memchr ();
#endif

#if !defined(HAVE_STPCPY)
char * stpcpy(char * dest, const char * src);
#endif

#if !defined(HAVE_STPNCPY)
char * stpncpy(char * dest, const char * src, size_t n);
#endif

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#if HAVE_ERROR && HAVE_ERROR_H
#include <error.h>
#endif

#if HAVE___SECURE_GETENV
#define	getenv(_s)	__secure_getenv(_s)
#endif

#ifdef STDC_HEADERS
/* FIX: shrug */
#define getopt system_getopt
#include <stdlib.h>
#undef getopt
#else /* not STDC_HEADERS */
char *getenv (const char *name);
#if ! HAVE_REALPATH
char *realpath(const char *path, char resolved_path []);
#endif
#endif /* STDC_HEADERS */

/* XXX solaris2.5.1 has not */
#if !defined(EXIT_FAILURE)
#define	EXIT_FAILURE	1
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#else
#include <sys/file.h>
#endif

#if !defined(SEEK_SET)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#if !defined(F_OK) 
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

#ifdef HAVE_DIRENT_H
# include <dirent.h>
# define NLENGTH(direct) (strlen((direct)->d_name))
#else /* not HAVE_DIRENT_H */
# define dirent direct
# define NLENGTH(direct) ((direct)->d_namlen)
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif /* HAVE_SYS_NDIR_H */
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif /* HAVE_SYS_DIR_H */
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif /* HAVE_NDIR_H */
#endif /* HAVE_DIRENT_H */

#include <ctype.h>

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

/* XXX FIXME: popt on sunos4.1.3: <sys/resource.h> requires <sys/time.h> */
#if HAVE_SYS_RESOURCE_H && HAVE_SYS_TIME_H
#include <sys/resource.h>
#endif

#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#if HAVE_GETOPT_H
#include <getopt.h>
#endif

#if HAVE_GRP_H
#include <grp.h>
#endif

#if HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef PATH_MAX
#ifdef _POSIX_PATH_MAX
#define PATH_MAX _POSIX_PATH_MAX
#elif defined MAXPATHLEN
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 256
#endif
#endif

#if HAVE_ERR_H
#include <err.h>
#endif

#if HAVE_MALLOC_H 
#include <malloc.h>
#endif

#if WITH_SELINUX
#include <selinux/selinux.h>
#else
typedef	char * security_context_t;

#define	freecon(_c)

#define	getfilecon(_fn, _c)	(-1)
#define	lgetfilecon(_fn, _c)	(-1)
#define	fgetfilecon(_fd, _c)	(-1)

#define	setfilecon(_fn, _c)	(-1)
#define	lsetfilecon(_fn, _c)	(-1)
#define	fsetfilecon(_fd, _c)	(-1)

#define	security_check_context(_c)	(0)

#define	is_selinux_enabled()	(0)

#define matchpathcon_init(_fn)			(-1)
#define matchpathcon_fini()			(0)
#define matchpathcon(_fn, _fm, _c)		(-1)

#define rpm_execcon(_v, _fn, _av, _envp)	(0)
#endif

/**
 * Wrapper to free(3), permit NULL, return NULL.
 * @param p             memory to free
 * @return              NULL always
 */
static inline
void * _free(void * p)
{
    if (p != NULL)      free(p);
    return NULL;
}

/**
 * Wrapper to free(3), permit NULL, return NULL. 
 * For documenting cases where const is used to protect long-lived 
 * non-const data that's supposed to be freed.
 * @param p             memory to free
 * @return              NULL always
 */
static inline
void * _constfree(const void * p)
{
    if (p != NULL)      free((void *)p);
    return NULL;
}

/* FIX: these are macros */
/**
 */
void * xmalloc (size_t size);

/**
 */
void * xcalloc (size_t nmemb, size_t size);

/**
 * @todo Annotate ptr with returned/out.
 */
void * xrealloc (void * ptr,
					size_t size);

/**
 */
char * xstrdup (const char *str);

/**
 */
void * vmefail(size_t size);

#if HAVE_MCHECK_H
#include <mcheck.h>
/* Memory allocation via macro defs to get meaningful locations from mtrace() */
#if defined(__GNUC__)
#define	xmalloc(_size) 		(malloc(_size) ? : vmefail(_size))
#define	xcalloc(_nmemb, _size)	(calloc((_nmemb), (_size)) ? : vmefail(_size))
#define	xrealloc(_ptr, _size)	(realloc((_ptr), (_size)) ? : vmefail(_size))
#define	xstrdup(_str)	(strcpy((malloc(strlen(_str)+1) ? : vmefail(strlen(_str)+1)), (_str)))
#endif	/* defined(__GNUC__) */
#endif	/* HAVE_MCHECK_H */

/* Retrofit glibc __progname */
#if defined __GLIBC__ && __GLIBC__ >= 2
#if __GLIBC_MINOR__ >= 1
#define	__progname	__assert_program_name
#endif
#define	setprogname(pn)
#else
#define	__progname	program_name
#define	setprogname(pn)	\
  { if ((__progname = strrchr(pn, '/')) != NULL) __progname++; \
    else __progname = pn;		\
  }
#endif
extern const char *__progname;

#if HAVE_NETDB_H
#include <netdb.h>
#endif

#if HAVE_PWD_H
#include <pwd.h>
#endif

/* Take care of NLS matters.  */

#if HAVE_LOCALE_H
# include <locale.h>
#endif
#if !HAVE_SETLOCALE
# define setlocale(Category, Locale) /* empty */
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) dgettext (PACKAGE, Text)
#else
# undef bindtextdomain
# define bindtextdomain(Domain, Directory) /* empty */
# undef textdomain
# define textdomain(Domain) /* empty */
# define _(Text) Text
# undef dgettext
# define dgettext(DomainName, Text) Text
#endif

#define N_(Text) Text

/* ============== from misc/miscfn.h */

#if !defined(USE_GNU_GLOB) 
#if HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

#if HAVE_GLOB_H 
#include <glob.h>
#endif
#else
#include "misc/glob.h"
#include "misc/fnmatch.h"
#endif

#if ! HAVE_S_IFSOCK
#define S_IFSOCK (0xc000)
#endif

#if ! HAVE_S_ISLNK
#define S_ISLNK(mode) ((mode & 0xf000) == S_IFLNK)
#endif

#if ! HAVE_S_ISSOCK
#define S_ISSOCK(mode) ((mode & 0xf000) == S_IFSOCK)
#endif

#if NEED_STRINGS_H
#include <strings.h>
#endif

#if NEED_MYREALLOC
#define realloc(ptr,size) myrealloc(ptr,size)
extern void *myrealloc(void *, size_t);
#endif

#if ! HAVE_SETENV
extern int setenv(const char *name, const char *value, int replace);
extern void unsetenv(const char *name);
#endif

#if HAVE_SYS_SOCKET_H
#include <sys/types.h>
#include <sys/socket.h>
#endif

#if HAVE_POLL_H
#include <poll.h>
#else
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#endif

/* Solaris <= 2.6 limits getpass return to only 8 chars */
#if HAVE_GETPASSPHRASE
#define	getpass	getpassphrase
#endif

#if ! HAVE_LCHOWN
#define lchown chown
#endif

#if HAVE_GETMNTINFO || HAVE_GETMNTINFO_R || HAVE_MNTCTL
# define GETMNTENT_ONE 0
# define GETMNTENT_TWO 0
# if HAVE_SYS_MNTCTL_H
#  include <sys/mntctl.h>
# endif
# if HAVE_SYS_VMOUNT_H
#  include <sys/vmount.h>
# endif
# if HAVE_SYS_MOUNT_H
#  include <sys/mount.h>
# endif
#elif HAVE_MNTENT_H || !(HAVE_GETMNTENT) || HAVE_STRUCT_MNTTAB
# if HAVE_MNTENT_H
#  include <stdio.h>
#  include <mntent.h>
#  define our_mntent struct mntent
#  define our_mntdir mnt_dir
# elif HAVE_STRUCT_MNTTAB
#  include <stdio.h>
#  include <mnttab.h>
   struct our_mntent {
       char * our_mntdir;
   };
   struct our_mntent *getmntent(FILE *filep);
#  define our_mntent struct our_mntent
# else
#  include <stdio.h>
   struct our_mntent {
       char * our_mntdir;
   };
   struct our_mntent *getmntent(FILE *filep);
#  define our_mntent struct our_mntent
# endif
# define GETMNTENT_ONE 1
# define GETMNTENT_TWO 0
#elif HAVE_SYS_MNTTAB_H
# include <stdio.h>
# include <sys/mnttab.h>
# define GETMNTENT_ONE 0
# define GETMNTENT_TWO 1
# define our_mntent struct mnttab
# define our_mntdir mnt_mountp
#else /* if !HAVE_MNTCTL */
# error Neither mntent.h, mnttab.h, or mntctl() exists. I cannot build on this system.
#endif

#ifndef MOUNTED
#define MOUNTED "/etc/mnttab"
#endif

#endif	/* H_SYSTEM */
