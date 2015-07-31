/* Copyright 2005-2008 Luis Furquim
 *
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

#ifndef CHIRONFS_FS_H
#define CHIRONFS_FS_H

#include "common.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif


#ifdef HAVE_GETMNTENT
#include <mntent.h>
#else
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif

#include "chiron-types.h"
#include "debug.h"
#include "utils.h"
#include "hash.h"

extern int      max_replica;
extern int      curr_replica;
extern int      max_replica_high;
extern int      curr_replica_high;
extern int      max_replica_low;
extern int      curr_replica_low;
extern int      max_replica_cache;
extern int      curr_replica_cache;
extern int    **round_robin_high;
extern int    **round_robin_low;
extern int    **round_robin_cache;
extern path_t  *paths;
extern char    *mount_point;
extern uint64_t   qt_hash_bits;
extern uint64_t   hash_mask;
extern char *chironctl_mountpoint;


#ifdef __linux__
#define CHIRON_LIMIT RLIMIT_OFILE
#define do_realpath(p,r) realpath(p,r)
#else
#define CHIRON_LIMIT RLIMIT_NOFILE
#endif

extern fd_t                    tab_fd;
extern long long unsigned int  FD_BUF_SIZE;
extern int  res, c, qtopt, fuse_argvlen;
extern char *argvbuf, *fuse_options, *fuse_arg, *fuse_argv[4];

void help(void);
void free_tab_fd(void);
int **mk_round_robin(int *tmp_list, int dim);
void free_paths(void);
void print_paths(void);
int do_mount(char *filesystems, char *mountpoint);
unsigned hash_fd(unsigned fd_main);
int fd_hashseekfree(unsigned fd_ndx);
int fd_hashset(int *fd);
int fd_hashseek(int fd_main);
#ifndef __linux__
char *do_realpath(const char *pathname, char *resolvedname);
#endif
int choose_replica(int try);
void disable_replica(int n);
void opt_parse(char *fo, char**log, char**argvbuf);
void printf_args(int argc, char**argv, int ndx);
void print_version(void);
char *chiron_realpath(char *path);
void free_round_robin(int **rr, int max_rep);
uint64_t hash64shift(uint64_t key);
uint32_t hash( uint32_t a);
int get_rights_by_name(const char *fname);
int get_rights_by_name_l(const char *fname);
int get_rights_by_fd(int fd);
int check_may_enter(char *fname);
int get_rights_by_mode(struct stat stb);
int chiron_mkdir(const char *path_orig, mode_t mode);
int chiron_getattr(const char *path, struct stat *stbuf);
void enable_replica(int n);
void trust_replica(int n);
void *start_ctl(void *arg);
void *chiron_init(void);

#endif /* CHIRONFS_FS_H */
