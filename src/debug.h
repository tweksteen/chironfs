#ifndef CHIRONFS_DEBUG_H
#define CHIRONFS_DEBUG_H

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

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
