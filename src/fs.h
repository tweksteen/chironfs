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

#include <getopt.h>


#if defined(_CHIRON_H_)
/* Defines the max filesystem replication */
int      max_replica        = 0;
int      curr_replica       = 0;
int      max_replica_high   = 0;
int      curr_replica_high  = 0;
int      max_replica_low    = 0;
int      curr_replica_low   = 0;
int      max_replica_cache  = 0;
int      curr_replica_cache = 0;
int    **round_robin_high   = NULL;
int    **round_robin_low    = NULL;
int    **round_robin_cache  = NULL;
path_t  *paths              = NULL;
char    *mount_point        = NULL;
uint64_t qt_hash_bits       = 0;
uint64_t hash_mask          = 0;
char *chironctl_mountpoint  = NULL;
#else
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

#endif

// Error codes
#define CHIRONFS_ERR_LOW_MEMORY        -1
#define CHIRONFS_ERR_LOG_ON_MOUNTPOINT -2
#define CHIRONFS_ERR_BAD_OPTIONS       -3
#define CHIRONFS_ERR_TOO_MANY_FOPENS   -4
#define CHIRONFS_ERR_BAD_LOG_FILE      -5
#define CHIRONFS_INVALID_PATH_MAX      -6
#define CHIRONFS_ADM_FORCED            -7


// 
// The lines below were adapted from a patch contributed by Antti Kantee
// to make ChironFS run on NetBSD
// Yen-Ming Lee has sent another patch, porting ChironFS to
// FreeBSD very similar to it
//
#ifdef __linux__

#define CHIRON_LIMIT RLIMIT_OFILE
#define do_realpath(p,r) realpath(p,r)

#else

#define CHIRON_LIMIT RLIMIT_NOFILE

#endif      
//
// End of BSD patch
//


#ifdef  __linux__
# define S_IREAD  S_IRUSR
# define S_IWRITE S_IWUSR
# define S_IEXEC  S_IXUSR
#endif


/*
#define chironctl_mountpoint        "/.chironctl"
#define strlen_chironctl_mountpoint 11
*/


#if defined(_CHIRON_H_)

// The tables
// file descriptor replica table
fd_t tab_fd = { NULL, 0 };

// Defines the size of chunks allocated to store the file descriptor replica tables
long long unsigned int FD_BUF_SIZE;

static struct option long_options[] =
{
   {"help",         0, 0, 'h'},
   {"version",      0, 0, 'V'},
   {"fsname",       1, 0, 'n'},
   {"log",          1, 0, 'l'},
   {"ctl",          1, 0, 'c'},
   {"fuseoptions",  1, 0, 'f'},
   {"o",            1, 0, 'o'},
   {"quiet",        0, 0, 'q'},
   {0, 0, 0, 0}
};

char short_options[] = "h?Vc:n:l:f:o:q";

/* getopt_long stores the option index here. */
int option_index = 0;


int  res, c, qtopt, fuse_argvlen=0;
char *argvbuf = NULL, *fuse_options = NULL, *fuse_arg=NULL, *fuse_argv[4];
char *myname;
char *ctlname   = NULL;
int   mount_ctl = 0;

#else

extern fd_t                    tab_fd;
extern long long unsigned int  FD_BUF_SIZE;
extern struct option          *long_options;
extern char                   *short_options;
extern int                     option_index;

extern int  res, c, qtopt, fuse_argvlen;
extern char *argvbuf, *fuse_options, *fuse_arg, *fuse_argv[4];
extern char *myname;
extern char *ctlname;
extern int   mount_ctl;

#endif


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
// 
// The lines below are from a patch contributed by Antti Kantee
// to make ChironFS run on NetBSD
// Yen-Ming Lee has sent another patch, porting ChironFS to
// FreeBSD very similar to it
//
#ifndef __linux__
char *do_realpath(const char *pathname, char *resolvedname);
#endif
//
// End of BSD patch
//
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
int read_a_line(char **buf, int *c, FILE *f);
void *start_ctl(void *arg);

#if defined(_CHIRON_H_)
void *chiron_init(struct fuse_conn_info *conn);
#endif


// static void chiron_destroy(void *notused);

/*
EIO,EBUSY,ENFILE,EMFILE,EFBIG,ENOSPC,EROFS,EDEADLK,ENOLCK,ENOSR,ENOLINK,
EBADFD,ENETDOWN,ENETUNREACH,ENETRESET,ECONNABORTED,ECONNRESET,ENOBUFS,
ETIMEDOUT,ECONNREFUSED,EHOSTDOWN,EHOSTUNREACH,ESTALE,EREMOTEIO,EDQUOT,
ENOMEDIUM,EMEDIUMTYPE,EOWNERDEAD,ENOTRECOVERABLE
*/
