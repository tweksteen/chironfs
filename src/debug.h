#ifndef CHIRONFS_DEBUG_H
#define CHIRONFS_DEBUG_H

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <libgen.h>
#include <stdint.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <pthread.h>

#include "chiron-types.h"
#include "fs.h"

#ifdef DEBUG
#define dbg(param, ...) debug(param, ##__VA_ARGS__)
#define timeval_subtract(res, x, y) timeval_sub(res, x, y)
#define gettmday(t,p) gettimeofday(t,p)
#define decl_tmvar(a,b,c) struct timeval a, b, c

void debug(const char *s, ...);
int timeval_sub (struct timeval *result, struct timeval *x, struct timeval *y);

#else

#define dbg(param, ...)
#define timeval_subtract(res, x, y)
#define gettmday(t,p)
#define decl_tmvar(a,b,c)

#endif

void print_err(int err, char *specifier);
void call_log(char *fnname, char *resource, int err);
void attach_log(void);

extern char                   *errtab[];
extern FILE    *logfd;
extern int      quiet_mode;
extern char    *logname;

#endif /* CHIRONFS_DEBUG_H */
