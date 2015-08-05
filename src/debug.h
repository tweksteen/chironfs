/* Copyright 2015 Thiébaud Weksteen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

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

#include "fs.h"

struct logger {
        FILE *logfd;
        int quiet;
};

extern struct logger logger;

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

void print_err(int, char *);
void _log(char *, char *, int);
void open_log(char *);

extern char                   *errtab[];

#endif /* CHIRONFS_DEBUG_H */
