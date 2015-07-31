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

#include "fs.h"

int max_replica          = 0;
int curr_replica         = 0;
int max_replica_high     = 0;
int curr_replica_high    = 0;
int max_replica_low      = 0;
int curr_replica_low     = 0;
int max_replica_cache    = 0;
int curr_replica_cache   = 0;
int **round_robin_high   = NULL;
int **round_robin_low    = NULL;
int **round_robin_cache  = NULL;
path_t *paths            = NULL;
char *mount_point        = NULL;
uint64_t qt_hash_bits    = 0;
uint64_t hash_mask       = 0;
char *chironctl_mountpoint  = NULL;

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
int  res, c, qtopt, fuse_argvlen=0;
char *argvbuf = NULL, *fuse_options = NULL, *fuse_arg=NULL, *fuse_argv[4];
char  *ctlname = NULL;
int   mount_ctl = 0;


unsigned hash_fd(unsigned fd_main)
{

	return(
	       (qt_hash_bits > 32)
	       ? (hash64shift(fd_main) & hash_mask)
	       : (hash(fd_main) & hash_mask)
	      );
}

int fd_hashseekfree(unsigned fd_ndx)
{
	unsigned i = fd_ndx;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	dbg("\nbufsz=%lx \thash=%lx",FD_BUF_SIZE,i);

	// If the hash address is used, search for an unused
	// starting at the next address, otherwise use it
	while ((tab_fd.fd[i]!=NULL) && (i<FD_BUF_SIZE)) {
		dbg("X");
		i++;
	}
	if (i<FD_BUF_SIZE) {
		dbg("\tused=%lx",i);

		return(i);
	}

	// If there is no free address until the end of the table
	// then restart the search from the beginning of the table
	i = 0;
	while ((tab_fd.fd[i]!=NULL) && (i<fd_ndx)) {
		i++;
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nhash time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	if (i<fd_ndx) {
		dbg("\tused=%lx",i);
		return(i);
	}

	// If there is no free address, return the error
	// We expect that it never happens because we set
	// the buffer to hold the file-max (the max opened
	// file system-wide)
	call_log("hash allocation","too many opened files",0);
	return(CHIRONFS_ERR_TOO_MANY_FOPENS);
}

int fd_hashset(int *fd)
{
	int i, fd_ndx = -1;

	for(i=0;i<max_replica;++i) {
		if ((!paths[i].disabled) && (fd[i]>=0)) {	
			dbg("\nTrying %d %d\n", i, fd[i]);
			fd_ndx = fd_hashseekfree(hash_fd(fd[i]));
			break;
		}
	}

	if (i==max_replica) {
		return(-1);
	}

	// If the hash address is not used then use it
	if (fd_ndx>=0) {
		tab_fd.fd[fd_ndx] = fd;
	}
	return(fd_ndx);
}

int choose_replica(int try)
{
	decl_tmvar(t1, t2, t3);
	gettmday(&t1,NULL);

	if (!try) {
		curr_replica_high++;
		if (curr_replica_high==max_replica_high) {
			curr_replica_high = 0;
		}
	}

	if (try==max_replica_high) {
		curr_replica_low++;
		if (curr_replica_low==max_replica_low) {
			curr_replica_low = 0;
		}
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nchoose_replica time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	if (try<max_replica_high) {
		dbg("\nhigh: try:  %u",try);
		dbg(",\trepl: %u",curr_replica_high);
		dbg(",\trr:   %u",round_robin_high[curr_replica_high][try]);
		return(round_robin_high[curr_replica_high][try]);
	}
	dbg("\nlow try:  %u",try);
	dbg(",\trepl: %u",curr_replica_low);
	dbg(",\trr:   %u",round_robin_low[curr_replica_low][try-max_replica_high]);
	return(round_robin_low[curr_replica_low][try-max_replica_high]);
}

void disable_replica(int n)
{
	if (n<0) {
		call_log("disabling replica",paths[-n].path,CHIRONFS_ADM_FORCED);
		paths[-n].disabled = time(NULL);
	} else {
		call_log("disabling replica",paths[n].path,0);
		paths[n].disabled = time(NULL);
	}
}

void enable_replica(int n)
{
	if (n<0) {
		call_log("enabling replica",paths[-n].path,CHIRONFS_ADM_FORCED);
		paths[-n].disabled = (time_t)1;
	} else {
		call_log("enabling replica",paths[n].path,0);
		paths[n].disabled = (time_t)1;
	}
}

void trust_replica(int n)
{
	if (n<0) {
		call_log("trusting replica",paths[-n].path,CHIRONFS_ADM_FORCED);
		paths[-n].disabled = (time_t)0;
	} else {
		call_log("trusting replica",paths[n].path,0);
		paths[n].disabled = (time_t)0;
	}
}


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//
//
//  P E R M I S S I O N   C H E C K   F U N C T I O N S
//
//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define get_ownership()                                    \
	context = fuse_get_context();                           \
gid     = context->gid;                                 \
slash   = strrchr(fname,'/');                           \
if (slash!=NULL) {                                      \
	if (slash==fname) {                                  \
		dir_stat = stat("/", &stbuf);                     \
	} else {                                             \
		(*slash) = 0;                                     \
		dir_stat = stat(fname, &stbuf);                   \
	}                                                    \
	if (!dir_stat) {                                     \
		if (stbuf.st_mode&02000) {                        \
			dbg("\nmode: %o",stbuf.st_mode);       \
			dbg("\ngid: %u",stbuf.st_gid);         \
			gid = stbuf.st_gid;                            \
		}                                                 \
	}                                                    \
	if (slash!=fname) {                                  \
		(*slash) = '/';                                   \
	}                                                    \
}


#define decl_get_rights_vars()               \
	static struct fuse_context *context;      \
struct stat    stbuf;                     \
int            i, res=0;                  \
struct group  result_buf, *result=NULL;   \
char          *buffer;                    \
struct passwd *pw;                        \
uid_t          pw_uid;                    \
int            perm;                      \
char         **member;                    \
context  = fuse_get_context()


#define get_rights(fn,file)                                       \
	if ((res=fn(file, &stbuf))<0) {                                \
		dbg("\nfn1 ret=%d file=%s",res,file);    \
		return(res);                                                \
	}

#define process_rights()                                          \
	dbg("\ncuid:%d\tstuid:%d",context->uid,stbuf.st_uid);  \
if (context->uid==stbuf.st_uid) {                              \
	perm = (stbuf.st_mode&0700) >> 6;                           \
} else if (context->gid==stbuf.st_gid) {                       \
	perm = (stbuf.st_mode&070) >> 3;                            \
} else {                                                       \
	i = 1024;                                                   \
	do {                                                        \
		buffer     = calloc(i,sizeof(char));                     \
		if (buffer!=NULL) {                                      \
			res = getgrgid_r(stbuf.st_gid,                        \
					 &result_buf,                          \
					 buffer, i,                           \
					 &result);                            \
			if (res==ERANGE) {                                    \
				free(buffer);                                      \
				i <<= 1;                                           \
			}                                                     \
		}                                                        \
	} while ((res==ERANGE) && (buffer!=NULL));                  \
	if (buffer==NULL) {                                         \
		errno = ENOMEM;                                          \
		return(-1);                                              \
	}                                                           \
	if (result!=NULL) {                                         \
		member = result->gr_mem;                                 \
		while (*member) {                                        \
			pw = getpwnam(*member);                                \
			pw_uid = pw->pw_uid;                                   \
			if (pw_uid==context->uid) {                            \
				perm = (stbuf.st_mode&070) >> 3;                    \
				break;                                              \
			}                                                      \
			member++;                                              \
		}                                                        \
		if (*member==NULL) {                                     \
			perm = stbuf.st_mode&7;                               \
		}                                                        \
	} else {                                                    \
		perm = stbuf.st_mode&7;                                  \
	}                                                           \
	free(buffer);                                               \
}


// verify if all directories in the path are allowed to enter (rwx, r-x or --x)
#define may_enter(filename)                          \
	dname = strdup(filename);                         \
if (dname==NULL) {                                \
	errno = ENOMEM;                                \
	return(-1);                                    \
}                                                 \
bkdname = dname;                                  \
do {                                              \
	dname = dirname(dname);                        \
	get_rights(stat,dname);                        \
	process_rights();                              \
	if (!(perm&1)) {                               \
		free(dname);                                \
		errno = EACCES;                             \
		dbg("\ndirperm: %d",perm);  \
		return(-1);                                 \
	}                                              \
} while ((dname!=NULL) && (strcmp(dname,"/")));   \
free(bkdname)



int get_rights_by_name(const char *fname)
{
	decl_get_rights_vars();
	char          *dname, *bkdname;
	if (!context->uid) {
		return(7);
	}
	may_enter(fname);
	get_rights(stat,fname);
	process_rights();
	dbg("\nperm: %d",perm);
	return(perm);
}


int get_rights_by_name_l(const char *fname)
{
	decl_get_rights_vars();
	char          *dname, *bkdname;
	if (!context->uid) {
		return(7);
	}
	may_enter(fname);
	get_rights(lstat,fname);
	process_rights();
	return(perm);
}


int get_rights_by_fd(int fd)
{
	decl_get_rights_vars();
	if (!context->uid) {
		return(7);
	}
	get_rights(fstat,fd);
	process_rights();
	return(perm);
}

int get_rights_by_mode(struct stat stb)
{
	decl_get_rights_vars();
	stbuf = stb;
	if (!context->uid) {
		return(7);
	}

	process_rights();
	dbg("\nperm: %d",perm);
	return(perm);
}


int check_may_enter(char *fname)
{
	decl_get_rights_vars();
	char          *dname, *bkdname;
	if (!context->uid) {
		return(0);
	}
	may_enter(fname);
	return(0);
}



////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//
//
//  W R A P P E R   F U N C T I O N S
//
//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


static int chiron_open(const char *path_orig, struct fuse_file_info *fi)
{
	char   *fname, *path, *slash, *dname;
	int     i, fd_ndx = -1, *fd, *err_list, perm, perm_mask=0;
	int     succ_cnt=0, fail_cnt=0, file_exists, dir_stat;
	unsigned int gid;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);
	decl_tmvar(t4, t5, t6);
	dbg("\nopen %s",path_orig);

	gettmday(&t1,NULL);


	fd = calloc(max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}

	path = strdup(path_orig);

	if (path!=NULL) {
		for(i=(max_replica-1);i>=0;--i) {
			gettmday(&t5,NULL);
			if (!paths[i].disabled) {
				fname = xlate(path,paths[i].path);
				if (fname!=NULL) {
					file_exists = lstat(fname, &stbuf);
					if (file_exists<0) {
						file_exists = (errno!=ENOENT);
					} else {
						file_exists = 1;
					}
					if ((!file_exists) && (fi->flags&O_CREAT)) {
						dname = strdup(fname);
						if (dname==NULL) {
							errno = ENOMEM;
							perm  = -1;
						} else {
							perm = get_rights_by_name(dirname(dname));
							free(dname);
						}
					} else {
						perm = get_rights_by_name(fname);
					}
					if (perm<0) {
						fail_cnt++;
						err_list[i] = -errno;
						fd[i] = -1;
					} else {
						if (fi->flags&(O_WRONLY|O_TRUNC|O_APPEND|O_CREAT)) {
							perm_mask = 2;
						} else if (fi->flags&O_RDWR) {
							perm_mask = 6;
						} else if ((fi->flags&(O_RDONLY|O_EXCL)) == (O_RDONLY)) {
							perm_mask = 4;
						}
						if (((perm&perm_mask)!=perm_mask) || (!perm_mask) || (!perm)) {
							fail_cnt++;
							err_list[i] = EACCES;
							dbg("\nfailopen, perm=%d, perm_mask=%d, fi->flags=0%o\n",perm,perm_mask,fi->flags);
						} else {
							fd[i] = open(fname,fi->flags);
							if (fd[i]<=0) {
								fail_cnt++;
								err_list[i] = errno;
							} else {
								if (!file_exists) {
									get_ownership();
									if (lchown(fname, context->uid, gid)==(-1)) {
										fail_cnt++;
										err_list[i] = -errno;
										close(fd[i]);
										fd[i] = -1;
									} else {
										succ_cnt++;
									}
								} else {
									succ_cnt++;
									dbg("\nopened fd=%x, perm=%d, perm_mask=%d, fi->flags=0%o\n",fd[i],perm,perm_mask,fi->flags);
								}
							}
						}
					}
					free(fname);
				} else {
					fail_cnt++;
					err_list[i] = -ENOMEM;
					fd[i] = -1;
				}
			}

			gettmday(&t4,NULL);
			timeval_subtract(&t6,&t5,&t4);
			dbg("\nopen replica time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

		}
		free(path);
		if (fail_cnt && succ_cnt) {
			for(i=0;i<max_replica;++i) {
				if (!paths[i].disabled) {
					if (fd[i]<0) {
						if (err_list[i]<0) {
							call_log("open+chown",paths[i].path,-err_list[i]);
						} else {
							call_log("open",paths[i].path,err_list[i]);
						}
						disable_replica(i);
					}
				}
			}
		}

		dbg("\nfd = %p [", fd);
		for(i=0;i<max_replica;i++)
			dbg("%x ", fd[i]);
		dbg("]\n");
		if (succ_cnt) {
			free(err_list);
			if ((fd_ndx=fd_hashset(fd))<0) {
				for(i=(max_replica-1);i>=0;--i) {
					if ((!paths[i].disabled) && (fd[i]>0)) {
						close(fd[i]);
					}
				}
				// fd_ndx = -ENOMEM;
			}
			if (fd_ndx < 0) {
				free( fd );        /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
				return -EMFILE;
			}
			//    we dont free fd anymore because it is being put in tab_fd. Now this will be freed only on close.
			fi->fh = fd_ndx;
			return(0);
		}
		gettmday(&t2,NULL);
		timeval_subtract(&t3,&t2,&t1);
		dbg("\nopen total time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

		for(i=(max_replica-1);i>=0;--i) {
			if (!paths[i].disabled) {
				if (err_list[i]) {
					dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);
					errno = err_list[i];
				}
			}
		}

	} else {
		errno = ENOMEM;
	}

	free(err_list);
	free( fd );           /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	return(-errno);
}

static int chiron_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.  This method is optional and can safely be left
	   unimplemented */

	int i, r;
	decl_tmvar(t1, t2, t3);

	(void) path;

	gettmday(&t1,NULL);
	dbg("release(%d)",fi->fh);
	if (tab_fd.fd[fi->fh]==NULL) {
		return(-EINVAL);
	}

	for(i=0,r=-1;i<max_replica;++i) {
		if ((tab_fd.fd[fi->fh][i]>0) && (!paths[i].disabled)) {
			r &= close(tab_fd.fd[fi->fh][i]);
			tab_fd.fd[fi->fh][i] = 0;
		}
	}
	free(tab_fd.fd[fi->fh]); // this is the fd chunk allocated in the chiron_open function
	tab_fd.fd[fi->fh] = NULL;

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nrelease time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	return(r);
}

static int chiron_read(const char *path, char *buf, size_t size,
		       off_t offset, struct fuse_file_info *fi)
{
	int     i, replica, *err_list, perm;
	ssize_t r;
	int     fail_cnt=0;
	decl_tmvar(t1, t2, t3);


	(void) path;

	gettmday(&t1,NULL);

	dbg("read(%d)",fi->fh);

	if (tab_fd.fd[fi->fh]==NULL) {
		return(-EINVAL);
	}

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		return -ENOMEM;
	}

	for(i=0;i<max_replica;++i) {
		replica = choose_replica(i);
		if ((tab_fd.fd[fi->fh][replica]>0) && (!paths[replica].disabled)) {
			perm = get_rights_by_fd(tab_fd.fd[fi->fh][replica]);
			if (perm<0) {
				err_list[replica] = errno;
				fail_cnt++;
			} else {
				if (!(perm&4)) {
					free(err_list);
					return(-EACCES);
				}
				r = pread(tab_fd.fd[fi->fh][replica],buf,size,offset);
				if (r>=0) {
					if (fail_cnt) {
						for(i=0;i<max_replica;++i) {
							if (err_list[i]) {
								call_log("read",paths[i].path,err_list[i]);
							}
						}
					}
					free(err_list);
					return(r);
				} else {
					err_list[replica] = errno;
					fail_cnt++;
				}
			}
		}
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nread time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	for(i=(max_replica-1);i>=0;--i) {
		if (!paths[i].disabled) {
			if (err_list[i]) {
				dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);
				errno = err_list[i];
			}
		}
	}

	free(err_list);
	return(-errno);
}

static int chiron_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	int     i, fail_cnt=0, succ_cnt=0, ret, *err_list, perm;
	ssize_t *w;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);
	(void) path;

	dbg("write(%d)",fi->fh);
	if (tab_fd.fd[fi->fh]==NULL) {
		return(-EINVAL);
	}

	w = calloc(max_replica,sizeof(ssize_t));
	if (w==NULL) {
		print_err(CHIRONFS_ERR_LOW_MEMORY,"replica return code table allocation");
		return(-ENOMEM);
	}
	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		free(w);
		print_err(CHIRONFS_ERR_LOW_MEMORY,"replica error code table allocation");
		return(-ENOMEM);
	}
	for(i=0;i<max_replica;++i) {
		if (!paths[i].disabled) {
			if (tab_fd.fd[fi->fh][i]>0) {
				perm = get_rights_by_fd(tab_fd.fd[fi->fh][i]);
				if (perm<0) {
					err_list[i] = errno;
					fail_cnt++;
				} else {
					if (!(perm&2)) {
						err_list[i] = EACCES;
						fail_cnt++;
					} else {
						w[i] = pwrite(tab_fd.fd[fi->fh][i],buf,size,offset);
						if (w[i]<0) {
							err_list[i] = errno;
							fail_cnt++;
						} else {
							succ_cnt++;
						}
					}
				}
			}
		}
	}

	if (fail_cnt && succ_cnt) {
		for(i=0;i<max_replica;++i) {
			if (!paths[i].disabled) {
				if (w[i]<0) {
					call_log("write",paths[i].path,err_list[i]);
					disable_replica(i);
				}
			}
		}
	}

	if (succ_cnt) {
		for(i=0;i<max_replica;++i) {
			if (!paths[i].disabled) {
				if (w[i]>=0) {
					ret = w[i];
					free( w );
					free(err_list);
					return(ret);
				}
			}
		}
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nwrite total time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	free( w );
	free(err_list);
	return(-errno);
}

#define dodir_byname_ro(fn,logmsgstr)                          \
	char *fname;                                                \
int i=0, st=-1, einval=0, qterr=0, staterr=0, replica;      \
int *err_list, perm;                                        \
decl_tmvar(t1, t2, t3);                                     \
gettmday(&t1,NULL);                                         \
err_list = calloc(max_replica,sizeof(int));                 \
if (err_list==NULL) {                                       \
	return -ENOMEM;                                          \
}                                                           \
do {                                                        \
	replica = choose_replica(i);                             \
	if (!paths[replica].disabled) {                          \
		fname = xlate(path,paths[replica].path);              \
		if (fname!=NULL) {                                    \
			perm = check_may_enter(fname);                     \
			if (perm<0) {                                      \
				qterr++;                                        \
				staterr = -errno;                               \
				err_list[replica] = errno;                      \
				free(fname);                                    \
			} else {                                           \
				st = fn;                                        \
				if (st == -1) {                                 \
					qterr++;                                     \
					staterr = -errno;                            \
					err_list[replica] = errno;                   \
					free(fname);                                 \
				} else {                                        \
					if (qterr) {                                 \
						for(i=0;i<max_replica;++i) {              \
							if (err_list[i]) {                     \
								call_log(logmsgstr,paths[i].path,err_list[i]);   \
							}                                      \
						}                                         \
					}                                            \
					gettmday(&t2,NULL);                          \
					timeval_subtract(&t3,&t2,&t1);               \
					dbg("\n%s succ time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec);  \
					free(err_list);                              \
					free(fname);                                 \
					return(st);                                  \
				}                                               \
			}                                                  \
		} else {                                              \
			einval++;                                          \
		}                                                     \
	}                                                        \
	++i;                                                     \
} while ((st<0) && (i<max_replica));                        \
gettmday(&t2,NULL);                                         \
timeval_subtract(&t3,&t2,&t1);                              \
dbg("\n%s fail time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec);  \
free(err_list);                                             \
if (qterr) {                                                \
	return(staterr);                                         \
}                                                           \
return(-EINVAL);


static int chiron_statfs(const char *path, struct statvfs *stbuf)
{
	dbg("\nstatfs: %s\n",path);
	dodir_byname_ro(statvfs(fname, stbuf),"statfs"); // fs info->superblock
}

int chiron_getattr(const char *path, struct stat *stbuf)
{
	dbg("\ngetattr: %s\n",path);
	dodir_byname_ro(lstat(fname, stbuf),"getattr");
}

static int chiron_access(const char *path, int mask)
{
	int ret, perm;
	struct stat stbuf;
	dbg("\naccess: %s\n",path);
	ret = chiron_getattr(path,&stbuf);

	if (ret<0) {
		return(ret);
	}

	perm = get_rights_by_mode(stbuf);
	if (perm>=0) {
		if ((perm&mask)==mask) {
			return(0);
		}
		return(-EACCES);
	}
	return(perm);
}


#define do_byname_ro(fn,logmsgstr)                          \
	char *fname;                                             \
int i=0, st=-1, einval=0, qterr=0, staterr=0, replica;   \
int *err_list, perm;                                     \
decl_tmvar(t1, t2, t3);                                  \
gettmday(&t1,NULL);                                      \
err_list = calloc(max_replica,sizeof(int));              \
if (err_list==NULL) {                                    \
	return -ENOMEM;                                       \
}                                                        \
do {                                                     \
	replica = choose_replica(i);                          \
	if (!paths[replica].disabled) {                       \
		fname = xlate(path,paths[replica].path);           \
		if (fname!=NULL) {                                 \
			perm = get_rights_by_name(fname);               \
			if (perm<0) {                                   \
				qterr++;                                     \
				staterr = -errno;                            \
				err_list[replica] = errno;                   \
				free(fname);                                 \
			} else {                                        \
				if (!(perm&4)) {                             \
					free(err_list);                           \
					free(fname);                              \
					return(-EACCES);                          \
				} else {                                     \
					st = fn;                                  \
					if (st == -1) {                           \
						qterr++;                               \
						staterr = -errno;                      \
						err_list[replica] = errno;             \
					} else {                                  \
						if (qterr) {                           \
							for(i=0;i<max_replica;++i) {        \
								if (err_list[i]) {               \
									call_log(logmsgstr,paths[i].path,err_list[i]);  \
								}                                \
							}                                   \
						}                                      \
						gettmday(&t2,NULL);                    \
						timeval_subtract(&t3,&t2,&t1);         \
						dbg("\n%s succ time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec); \
						free(err_list);                        \
						free(fname);                           \
						return(st);                            \
					}                                         \
				}                                            \
				free(fname);                                 \
			}                                               \
		} else {                                           \
			einval++;                                       \
		}                                                  \
	}                                                     \
	++i;                                                  \
} while ((st<0) && (i<max_replica));                     \
gettmday(&t2,NULL);                                      \
timeval_subtract(&t3,&t2,&t1);                           \
dbg("\n%s fail time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec); \
free(err_list);                                          \
if (qterr) {                                             \
	return(staterr);                                      \
}                                                        \
return(-EINVAL)


/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
static int chiron_readlink(const char *path, char *buf, size_t size)
{
	dbg("\nreadlink: %s\n",path);

	//   do_byname_ro(readlink(fname, buf, size));
	char *fname;
	int i=0, st=-1, einval=0, qterr=0, staterr=0, replica;
	int *err_list, perm;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		return -ENOMEM;
	}

	do {
		replica = choose_replica(i);
		if (!paths[replica].disabled) {
			fname = xlate(path,paths[replica].path);
			if (fname!=NULL) {
				perm = get_rights_by_name_l(fname);
				if (perm<0) {
					qterr++;
					staterr = -errno;
					err_list[replica] = errno;
					free(fname);
				} else {
					if (!(perm&4)) {
						free(err_list);
						free(fname);
						return(-EACCES);
					}
					st = readlink(fname, buf, size);
					free(fname);
					if (st == -1) {
						qterr++;
						staterr = -errno;
						err_list[replica] = errno;
					} else {
						if (qterr) {
							for(i=0;i<max_replica;++i) {
								if (err_list[i]) {
									call_log("readlink",paths[i].path,err_list[i]);
								}
							}
						}
						free(err_list);
						gettmday(&t2,NULL);
						timeval_subtract(&t3,&t2,&t1);
						dbg("\nreadlink succ time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);
						buf[st] = 0;
						return(0);
					}
				}
			} else {
				einval++;
			}
		}
		++i;
	} while ((st<0) && (i<max_replica));
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nreadlink fail time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	if (qterr) {
		return(staterr);
	}
	return(-EINVAL);
}


static int chiron_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	char *fname;
	int            i=0, einval=0, qterr=0, staterr=0, replica, perm;
	DIR           *dp = NULL;
	struct dirent *de;
	int           *err_list;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	//   (void) offset;
	(void) fi;
	dbg("\nreaddir: %s, offset: %ld\n",path,offset);

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		return -ENOMEM;
	}

	do {
		replica = choose_replica(i);
		if (!paths[replica].disabled) {
			fname   = xlate(path,paths[replica].path);
			if (fname!=NULL) {
				perm = get_rights_by_name(fname);
				if (perm<0) {
					qterr++;
					staterr = -errno;
					err_list[replica] = errno;
				} else {
					if (!(perm&4)) {
						free(err_list);
						free(fname);
						return(-EACCES);
					}
					dp = opendir(fname);
					if (dp == NULL) {
						qterr++;
						staterr = -errno;
						err_list[replica] = errno;
					}
				}
				free(fname);
			} else {
				einval++;
			}
		}
		++i;
	} while ((dp==NULL) && (i<max_replica));


	if (dp==NULL) {
		gettmday(&t2,NULL);
		timeval_subtract(&t3,&t2,&t1);
		dbg("\nreaddir fail time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

		free(err_list);
		if (qterr) {
			return(staterr);
		}
		return(-EINVAL);
	}


	if (qterr) {
		for(i=0;i<max_replica;++i) {
			if (err_list[i]) {
				call_log("readdir",paths[i].path,err_list[i]);
			}
		}
	}
	free(err_list);

	seekdir(dp, offset);
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nreaddir succ time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	return 0;
}

static int chiron_mknod(const char *path_orig, mode_t mode, dev_t rdev)
{
	char   *fname, *path, *slash, *dname;
	int     i, *fd, succ_cnt=0, fail_cnt=0, *err_list, dir_stat, perm;
	unsigned int gid;
	(void) rdev;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	dbg("\nmknod@: %s\n",path_orig);
	fd = calloc(max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}

	path = strdup(path_orig);

	if (path!=NULL) {
		for(i=(max_replica-1);i>=0;--i) {
			if (!paths[i].disabled) {
				fname = xlate(path,paths[i].path);
				if (fname!=NULL) {
					dname = strdup(fname);
					if (dname==NULL) {
						perm  = -1;
						errno = ENOMEM;
					} else {
						perm = get_rights_by_name(dirname(dname));
						free(dname);
					}
					if (perm<0) {
						fail_cnt++;
						err_list[i] = errno;
						fd[i] = -1;
					} else if (!(perm&2)) {
						fail_cnt++;
						err_list[i] = EACCES;
						fd[i] = -1;
					} else {
						if (S_ISREG(mode)) {
							fd[i] = open(fname, O_CREAT | O_EXCL | O_WRONLY, mode);
							if (fd[i] >= 0) {
								fd[i] = close(fd[i]);
								get_ownership();
								if (lchown(fname, context->uid, gid)==(-1)) {
									fail_cnt++;
									err_list[i] = -errno;
									fd[i] = -1;
								} else {
									succ_cnt++;
								}
								dbg("\nmknod/open+chown: %s\n",path_orig);
							} else {
								err_list[i] = errno;
								fail_cnt++;
							}
						} else {
							if (S_ISFIFO(mode)) {
								fd[i] = mkfifo(fname, mode);
							} else {
								context = fuse_get_context();
								if (context->uid) {
									fd[i] = -1 ;
									errno = EPERM;
								} else {
									fd[i] = mknod(fname, mode, rdev);
								}
							}
							if (fd[i]==0) {
								get_ownership();
								if (lchown(fname, context->uid, gid)==(-1)) {
									fail_cnt++;
									err_list[i] = -errno;
									fd[i] = -1;
								} else {
									succ_cnt++;
								}
								dbg("\nmknod/fifo/nod+chown: %s\n",path_orig);
							} else {
								err_list[i] = errno;
								fail_cnt++;
							}
						}
					}
					free(fname);
				}
			}
		}
		free(path);

		if (fail_cnt && succ_cnt) {
			for(i=0;i<max_replica;++i) {
				if (!paths[i].disabled) {
					if (fd[i]<0) {
						if (err_list[i]<0) {
							call_log("mknod+chown",paths[i].path,-err_list[i]);
						} else {
							call_log("mknod",paths[i].path,err_list[i]);
						}
						disable_replica(i);
					}
				}
			}
		}

		gettmday(&t2,NULL);
		timeval_subtract(&t3,&t2,&t1);
		dbg("\nmknod time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

		if (succ_cnt) {
			dbg("\nmknod nofail");
			free( fd );             /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
			free(err_list);

			return(0);
		}
		dbg("\nmknod fail0");
		for(i=(max_replica-1);i>=0;--i) {
			if (!paths[i].disabled) {
				if (err_list[i]) {
					dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);
					errno = err_list[i];
				}
			}
		}
	} else {
		errno = ENOMEM;
	}

	dbg("\nmknod fail1");
	free( fd );               /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	free(err_list);
	return -errno;
}


#define do_byname_rw(fn,logmsgstr,perm_check,permop,restrict,nerrno)                           \
	char   *fname, *path;                                                                       \
int     i, *fd;                                                                             \
int     fail_cnt=0, succ_cnt=0;                                                             \
int    *err_list, perm;                                                                     \
decl_tmvar(t1, t2, t3);                                                                     \
gettmday(&t1,NULL);                                                                         \
fd = calloc(max_replica,sizeof(int));                                                       \
if (fd==NULL) {                                                                             \
	return -ENOMEM;                                                                          \
}                                                                                           \
err_list = calloc(max_replica,sizeof(int));                                                 \
if (err_list==NULL) {                                                                       \
	free(fd);                                                                                \
	return -ENOMEM;                                                                          \
}                                                                                           \
for(i=0;i<max_replica;++i) {                                                                \
	err_list[i] = 0;                                                                         \
}                                                                                           \
path = strdup(path_orig);                                                                   \
if (path!=NULL) {                                                                           \
	for(i=(max_replica-1);i>=0;--i) {                                                        \
		if (!paths[i].disabled) {                                                             \
			fname = xlate(path,paths[i].path);                                                 \
			if (fname!=NULL) {                                                                 \
				perm = perm_check;                                                              \
				if (perm<0) {                                                                   \
					err_list[i] = errno;                                                         \
					fail_cnt++;                                                                  \
					dbg("\n%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);          \
				} else if ((!(perm&2)) permop (restrict)) {                                     \
					err_list[i] = nerrno;                                                        \
					fail_cnt++;                                                                  \
					dbg("\n%s try %d, errno: %d, perm: %x\n",logmsgstr,i,nerrno,perm);         \
				} else {                                                                        \
					fd[i] = fn;                                                                  \
					free(fname);                                                                 \
					if (fd[i]==0) {                                                              \
						succ_cnt++;                                                               \
						err_list[i] = 0;                                                          \
						dbg("\n%s succ %d, perm: %x\n",logmsgstr,i,perm);                          \
					} else {                                                                     \
						err_list[i] = errno;                                                      \
						fail_cnt++;                                                               \
						dbg("\n%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);          \
					}                                                                            \
				}                                                                               \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	free(path);                                                                              \
	if (fail_cnt && succ_cnt) {                                                              \
		for(i=0;i<max_replica;++i) {                                                          \
			if (!paths[i].disabled) {                                                          \
				if (fd[i]<0) {                                                                  \
					call_log(logmsgstr,paths[i].path,err_list[i]);                               \
					disable_replica(i);                                                          \
				}                                                                               \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	for(i=(max_replica-1);i>=0;--i) {                                                        \
		if (!paths[i].disabled) {                                                             \
			if (err_list[i]) {                                                                 \
				dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);                       \
				return(-err_list[i]);                                                           \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */ \
	free( err_list );                                                                        \
	gettmday(&t2,NULL);                                                                      \
	timeval_subtract(&t3,&t2,&t1);                                                           \
	dbg("\n%s time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec);                  \
	if (succ_cnt) {                                                                          \
		return(0);                                                                            \
	}                                                                                        \
	return -errno;                                                                           \
}                                                                                           \
free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */    \
free( err_list );                                                                           \
gettmday(&t2,NULL);                                                                         \
timeval_subtract(&t3,&t2,&t1);                                                              \
dbg("\n%s allocfail time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec);           \
return -ENOMEM


static int chiron_truncate(const char *path_orig, off_t size)
{
	dbg("\ntruncate: %s\n",path_orig);
	do_byname_rw(truncate(fname, size), "truncate",get_rights_by_name(fname),||,0,EACCES);
}

static int chiron_chmod(const char *path_orig, mode_t mode)
{
	static struct fuse_context *context;
	struct stat stbuf;
	int st;

	context = fuse_get_context();

	if ((st=chiron_getattr(path_orig, &stbuf))<0) {
		return(st);
	}
	dbg("\nchmod: %s\n",path_orig);
	do_byname_rw(chmod(fname, mode), "chmod",2,||,(
						       // deny if user is not privileged neither the owner
						       (context->uid && (stbuf.st_uid!=context->uid))
						      ),EPERM);
}

static int chiron_chown(const char *path_orig, uid_t uid, gid_t gid)
{
	static struct fuse_context *context;
	struct stat stbuf;
	int st;

	context = fuse_get_context();

	if ((st=chiron_getattr(path_orig, &stbuf))<0) {
		return(st);
	}
	dbg("\nchown: %s\n",path_orig);
	do_byname_rw(lchown(fname, uid, gid), "chown",get_rights_by_name(fname),||,(
										    // deny if system is restricted user is not privileged and is trying to change the owner
										    (_POSIX_CHOWN_RESTRICTED && context->uid && (stbuf.st_uid!=uid))
										    ||
										    // deny if user is not privileged neither the owner and is trying to change the owner
										    (context->uid && (stbuf.st_uid!=context->uid) && (stbuf.st_uid!=uid))
										    ||
										    // deny if user is not privileged neither the owner and is trying to change the group
										    (context->uid && (stbuf.st_uid!=context->uid) && (stbuf.st_gid!=gid))
										   ),EPERM);
}

static int chiron_utime(const char *path_orig, struct utimbuf *buf)
{
	static struct fuse_context *context;
	struct stat stbuf;
	int st;

	context = fuse_get_context();

	if ((st=chiron_getattr(path_orig, &stbuf))<0) {
		return(st);
	}
	dbg("\nutime: %s\n",path_orig);
	do_byname_rw(utime(fname, buf), "utime",get_rights_by_name(fname),&&,(
									      // deny if doesn't happens that buf==NULL and user is not privileged neither the owner
									      (
									       //       (dbg(("\nperm=%d,buf=%d,cuid=%d,fuid=%d",perm,buf,context->uid,stbuf.st_uid )) )
									       //       ,
									       (! ((buf==NULL) && (context->uid || (stbuf.st_uid==context->uid))) )
									      )
									     ),EACCES);
}

static int chiron_rmdir(const char *path_orig)
{
	char *dname;
	int tmpperm;
	dbg("\nrmdir: %s\n",path_orig);
	do_byname_rw(rmdir(fname), "rmdir",(
					    ((dname=strdup(fname))==NULL)
					    ? ( errno = ENOMEM, -1 )
					    : ( tmpperm = get_rights_by_name(dirname(dname)), free(dname), tmpperm)
					   ),||,0,EACCES);
}

static int chiron_unlink(const char *path_orig)
{
	char *dname;
	int tmpperm;
	dbg("\nunlink: %s\n",path_orig);
	do_byname_rw(unlink(fname), "unlink",(
					      ((dname=strdup(fname))==NULL)
					      ? ( errno = ENOMEM, -1 )
					      : ( tmpperm = get_rights_by_name(dirname(dname)), free(dname), tmpperm)
					     ),||,0,EACCES);
}

int chiron_mkdir(const char *path_orig, mode_t mode)
{
	dbg("\nmkdir: %s\n",path_orig);
	char   *fname, *path, *slash, *dname;
	int     i, *fd;
	int     fail_cnt=0, succ_cnt=0, dir_stat;
	int    *err_list, perm;
	unsigned int gid;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	context = fuse_get_context();

	fd = calloc(max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}
	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}
	path = strdup(path_orig);
	if (path!=NULL) {
		for(i=(max_replica-1);i>=0;--i) {
			if (!paths[i].disabled) {
				fname = xlate(path,paths[i].path);
				if (fname!=NULL) {
					dname = strdup(fname);
					if (dname==NULL) {
						perm  = -1;
						errno = ENOMEM;
					} else {
						perm = get_rights_by_name(dirname(dname));
						free(dname);
					}
					if (perm<0) {
						fail_cnt++;
						err_list[i] = errno;
						fd[i] = -1;
					} else if (!(perm&2)) {
						fail_cnt++;
						err_list[i] = EACCES;
						fd[i] = -1;
					} else {
						fd[i] = mkdir(fname, mode);
						if (fd[i]==0) {
							get_ownership();
							if (lchown(fname, context->uid, gid)==(-1)) {
								fail_cnt++;
								err_list[i] = -errno;
								fd[i] = -1;
							} else {
								succ_cnt++;
							}
						} else {
							err_list[i] = errno;
							fail_cnt++;
						}
					}
					free(fname);
				}
			}
		}
		free(path);
		if (fail_cnt && succ_cnt) {
			for(i=0;i<max_replica;++i) {
				if (!paths[i].disabled) {
					if (fd[i]<0) {
						if (err_list[i]<0) {
							call_log("mkdir+chown",paths[i].path,-err_list[i]);
						} else {
							call_log("mkdir",paths[i].path,err_list[i]);
						}
						disable_replica(i);
					}
				}
			}
		}

		gettmday(&t2,NULL);
		timeval_subtract(&t3,&t2,&t1);
		dbg("\nmkdir time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

		if (succ_cnt) {
			free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
			free( err_list );
			return(0);
		}
		for(i=(max_replica-1);i>=0;--i) {
			if (!paths[i].disabled) {
				if (err_list[i]) {
					dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);
					errno = err_list[i];
				}
			}
		}
	} else {
		errno = ENOMEM;
	}
	free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	free( err_list );

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nmkdir alloc fail time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	return -errno;

}

static int chiron_symlink(const char *from, const char *to)
{
	char *fname, *slash, *dname;
	int   i, *fd, fail_cnt=0, succ_cnt=0, *err_list, dir_stat, perm;
	unsigned int gid;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	dbg("\nsymlink: %s->%s\n",from,to);
	context = fuse_get_context();
	fd = calloc(max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}

	err_list = calloc(max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}

	for(i=(max_replica-1);i>=0;--i) {
		if (!paths[i].disabled) {
			fd[i] = 0;
			fname = xlate(to,paths[i].path);
			if (fname!=NULL) {
				dname = strdup(fname);
				if (dname==NULL) {
					perm  = -1;
					errno = ENOMEM;
				} else {
					perm = get_rights_by_name(dirname(dname));
					free(dname);
				}
				if (perm<0) {
					fail_cnt++;
					err_list[i] = errno;
					fd[i] = -1;
				} else if (!(perm&2)) {
					fail_cnt++;
					err_list[i] = EACCES;
					fd[i] = -1;
				} else {
					fd[i] = symlink(from, fname);
					if (fd[i]<0) {
						fail_cnt++;
						err_list[i] = errno;
					} else {
						get_ownership();
						if (lchown(fname, context->uid, gid)==(-1)) {
							fail_cnt++;
							err_list[i] = -errno;
							fd[i] = -1;
						} else {
							succ_cnt++;
						}
					}
				}
				free(fname);
			}
		}
	}

	if (fail_cnt && succ_cnt) {
		for(i=0;i<max_replica;++i) {
			if (!paths[i].disabled) {
				if (fd[i]<0) {
					if (err_list[i]<0) {
						call_log("symlink+chown",paths[i].path,-err_list[i]);
					} else {
						call_log("symlink",paths[i].path,err_list[i]);
					}
					disable_replica(i);
				}
			}
		}
	}

	for(i=(max_replica-1);i>=0;--i) {
		if (!paths[i].disabled) {
			if (err_list[i]) {
				dbg("\nretval: %d,%d,%d\n",-err_list[i],err_list[i],i);
				errno = err_list[i];
			}
		}
	}

	free( fd );         /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("\nsymlink time %ld secs,  %ld usecs",t3.tv_sec,t3.tv_usec);

	if (succ_cnt) {
		return(0);
	}

	return -errno;
}

#define do_by2names_rw(perm_from,fn,logmsgstr)                             \
	char   *fname_from=NULL, *fname_to=NULL, *dname;                        \
int     i, *fd, fail_cnt=0, succ_cnt=0, perm;                           \
decl_tmvar(t1, t2, t3);                                                 \
gettmday(&t1,NULL);                                                     \
fd = calloc(max_replica,sizeof(int));                                   \
if (fd==NULL) {                                                         \
	return -ENOMEM;                                                      \
}                                                                       \
for(i=(max_replica-1);i>=0;--i) {                                       \
	if (!paths[i].disabled) {                                            \
		fd[i] = 0;                                                        \
		fname_from = xlate(from,paths[i].path);                           \
		if (fname_from==NULL) {                                           \
			perm  = -1;                                                    \
			errno = -ENOMEM;                                               \
		} else {                                                          \
			dname = strdup(fname_from);                                    \
			if (dname==NULL) {                                             \
				perm  = -1;                                                 \
				errno = -ENOMEM;                                            \
			} else {                                                       \
				perm = get_rights_by_name(dirname(dname));                  \
				free(dname);                                                \
			}                                                              \
		}                                                                 \
		if (perm<0) {                                                     \
			fd[i] = -errno;                                                \
			fail_cnt++;                                                    \
			dbg("\n1. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
		} else if (((perm&perm_from)!=perm_from) && perm_from) {          \
			fd[i] = -EACCES;                                               \
			fail_cnt++;                                                    \
			dbg("\n1. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,EACCES,perm);           \
		} else {                                                          \
			fname_to = xlate(to,paths[i].path);                            \
			if (fname_to==NULL) {                                          \
				perm  = -1;                                                 \
				errno = -ENOMEM;                                            \
			} else {                                                       \
				dname      = strdup(fname_from);                            \
				if (dname==NULL) {                                          \
					perm  = -1;                                              \
					errno = -ENOMEM;                                         \
				} else {                                                    \
					perm = get_rights_by_name(dirname(dname));               \
					free(dname);                                             \
				}                                                           \
			}                                                              \
			if (perm<0) {                                                  \
				fd[i] = -errno;                                             \
				fail_cnt++;                                                 \
				dbg("\n2. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
			} else if ((perm&3)!=3) {                                        \
				fd[i] = -EACCES;                                            \
				fail_cnt++;                                                 \
				dbg("\n2. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,EACCES,perm);           \
			} else {                                                       \
				fd[i] = fn;                                                 \
				if (fd[i]==0) {                                             \
					succ_cnt++;                                              \
					dbg("\n%s succ %d, perm: %x\n",logmsgstr,i,perm);                            \
				} else {                                                    \
					fd[i] = -errno;                                          \
					fail_cnt++;                                              \
					dbg("\n%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
				}                                                           \
			}                                                              \
		}                                                                 \
		if (fname_from!=NULL) {                                           \
			free(fname_from);                                              \
		}                                                                 \
		if (fname_to!=NULL) {                                             \
			free(fname_to);                                                \
		}                                                                 \
	}                                                                    \
}                                                                       \
if (fail_cnt && succ_cnt) {                                             \
	for(i=0;i<max_replica;++i) {                                         \
		if (!paths[i].disabled) {                                         \
			if (fd[i]<0) {                                                 \
				call_log(logmsgstr,paths[i].path,-fd[i]);                   \
				disable_replica(i);                                         \
			}                                                              \
		}                                                                 \
	}                                                                    \
}                                                                       \
gettmday(&t2,NULL);                                                     \
timeval_subtract(&t3,&t2,&t1);                                          \
dbg("\n%s time %ld secs,  %ld usecs",logmsgstr,t3.tv_sec,t3.tv_usec); \
if (succ_cnt) {                                                         \
	free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */  \
	return(0);                                                                                \
}                                                                                            \
for(i=(max_replica-1);i>=0;--i) {                                                            \
	if (!paths[i].disabled) {                                                                 \
		if (fd[i]) {                                                                           \
			dbg("\nretval: %d,%d,%d\n",-fd[i],fd[i],i);                                       \
			errno = fd[i];                                                                      \
		}                                                                                      \
	}                                                                                         \
}                                                                                            \
free( fd );   /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */   \
return(errno)

static int chiron_rename(const char *from, const char *to)
{
	int st, dperm;
	struct stat stbuf;

	if ((st=chiron_getattr(from, &stbuf))<0) {
		return(st);
	}
	dbg("\nrename: mode:%o\tdir:%d\n",stbuf.st_mode,S_ISDIR(stbuf.st_mode));
	if (S_ISDIR(stbuf.st_mode)) {
		dbg("\nrename: %s isdir\n",from);
		dperm = get_rights_by_mode(stbuf);
		if (dperm<0) {
			return(-errno);
		}
		dbg("\nrename: fromperm=%x\n",dperm);
		if (!(dperm&2)) {
			return(-EACCES);
		}
	}

	if ((st=chiron_getattr(to, &stbuf))>=0) {
		if (S_ISDIR(stbuf.st_mode)) {
			dbg("\nrename: %s isdir\n",to);
			dperm = get_rights_by_mode(stbuf);
			if (dperm<0) {
				return(dperm);
			}
			dbg("\nrename: toperm=%x\n",dperm);
			if (!(dperm&2)) {
				return(-EACCES);
			}
		}
	}
	dbg("\nrename: %s->%s\n",from,to);
	do_by2names_rw(3,rename(fname_from, fname_to), "rename");
}

static int chiron_link(const char *from, const char *to)
{
	dbg("\nlink: %s->%s\n",from,to);
	do_by2names_rw(0,link(fname_from, fname_to), "link");
}

static int chiron_fsync(const char *path, int isdatasync,
			struct fuse_file_info *fi)
{
	/* Just a stub.  This method is optional and can safely be left
	   unimplemented */
	//   int i, *fd, fail_cnt=0, succ_cnt=0, ret;

	(void) path;
	(void) isdatasync;
	(void) fi;
	dbg("\nfsync: %d\n",fi->fh);
	/*
	   if (tab_fd.fd[fi->fh]==NULL) {
	   return(-EINVAL);
	   }

	   fd = calloc(max_replica,sizeof(int));
	   if (fd==NULL) {
	   return -ENOMEM;
	   }

	   for(i=(max_replica-1);i>=0;--i) {
	   if (!paths[i].disabled) {
	   if (isdatasync) {
	   fd[i] = fdatasync(tab_fd.fd[fi->fh][i]);
	   } else {
	   fd[i] = fsync(tab_fd.fd[fi->fh][i]);
	   }
	   if (fd[i]<0) {
	   fd[i] = -errno;
	   }
	   }
	   }

	   if (fail_cnt && succ_cnt) {
	   for(i=0;i<max_replica;++i) {
	   if (!paths[i].disabled) {
	   if (fd[i]<0) {
	   call_log("fsync",paths[i].path,-fd[i]);
	   disable_replica(i);
	   }
	   }
	   }
	   }
	   */

	//   free( fd );      /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	//   if (succ_cnt) {
	//      return(0);
	//   }

	//   return(-errno);

	return(0);
}


//////////////////////////////////////



/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 */
void *chiron_init()
{
	struct stat st;
	pthread_t   thand;
	int         i;

	if (mount_ctl) {
		dbg("\nctl: struct stat initialized");
		i = stat(chironctl_mountpoint,&st);
		dbg("\nctl: getattr=%d",i);
		if (i==ENOENT) {
			dbg("\nctl: NOENT");
			i = mkdir(chironctl_mountpoint,S_IFDIR | S_IRUSR | S_IXUSR);
			dbg("\nctl: mkdir=%d",i);
		}

		if (i) {
			dbg("\nctl: nomkdir");
			call_log("control directory",chironctl_mountpoint,-i);                   \
				return(NULL);
		}

		if (pthread_create(&thand,NULL,start_ctl, NULL) != 0) {
			// must end program here
			return(NULL);
		}
	}
	return(NULL);
}



/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
static int chiron_flush(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
	return(0);
}



////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
int (*fsyncdir) (const char *, int, struct fuse_file_info *);

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
//    int (*ftruncate) (const char *, off_t, struct fuse_file_info *);

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int (*fgetattr) (const char *, struct stat *, struct fuse_file_info *);


#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int chiron_setxattr(const char *path, const char *name, const char *value,
			   size_t size, int flags)
{
	char *logmsg;
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int chiron_getxattr(const char *path, const char *name, char *value,
			   size_t size)
{
	char *logmsg;
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int chiron_listxattr(const char *path, char *list, size_t size)
{
	char *logmsg;
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int chiron_removexattr(const char *path, const char *name)
{
	char *logmsg;
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}

#endif /* HAVE_SETXATTR */


static void chiron_destroy(void *notused)
{
	(void) notused;
	free_paths();
}

void *start_ctl(void *arg)
{
	// parent writes, son reads
	int ctlpipe_parson[2];

	// son writes, parent reads
	int ctlpipe_sonpar[2];

	// parent decriptors
	FILE *fi, *fo;

	char *buf = NULL;
	int   i, sz, someerr;
	//   struct tm t;

	(void) arg;

	dbg("\nthread: started\n");

	if (pipe(ctlpipe_parson)<0) {
		pthread_exit("error");
	}

	if (pipe(ctlpipe_sonpar)<0) {
		pthread_exit("error");
	}

	pid_t pid;
	if ((pid = fork())==0) {
		// I am the child
		close(ctlpipe_parson[1]);
		close(ctlpipe_sonpar[0]);
		dbg("\nfork: I am the child\n");

		if (ctlpipe_parson[0] != STDIN_FILENO) {
			dup2 (ctlpipe_parson[0], STDIN_FILENO);
			close (ctlpipe_parson[0]);
		}
		if (ctlpipe_sonpar[1] != STDOUT_FILENO) {
			dup2 (ctlpipe_sonpar[1], STDOUT_FILENO);
			close (ctlpipe_sonpar[1]);
		}
		// After forking, run the prog who implements the
		// proc-like filesystem to control the main chironfs
		dbg("\nchild: execing %s\n",ctlname);
		execlp(ctlname,ctlname,NULL);
		dbg("\nchild: failed execing %s error is %d\n",ctlname,errno);
	} else if (pid<0) {
		pthread_exit("error");
	}
	// I am the parent
	close(ctlpipe_parson[0]);
	close(ctlpipe_sonpar[1]);

	dbg("\nfork: I am the parent\n");
	fi = fdopen(ctlpipe_sonpar[0],"r");
	fo = fdopen(ctlpipe_parson[1],"w");

	while (1) {
		someerr = read_a_line(&buf,&sz,fi);
		if (someerr) {
			pthread_exit("error");
		}
		dbg("\nparent: got a msg %s\n",buf);
		if (strncmp(buf,"stat:",5)==0) {
			// status request
			for(i=0;i<max_replica;++i) {
				sscanf(buf+5,"%02X",&i);
				if ((i>=0)&&(i<max_replica)) {
					if (((int)paths[i].disabled)<2) {
						fprintf(fo,"0001%1X",((int)paths[i].disabled)); // enabled, but untrusted
					} else {
						//                  localtime_r(((time_t *)(&paths[i].disabled)),&t);
						//                  fprintf(fo,"000E%04d%02d%02d%02d%02d%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec); // disabled
						fprintf(fo,"00012"); // disabled
					}
					fflush(fo);
					break;
				}
			}
		} else if (strncmp(buf,"disable:",8)==0) {
			// put replica in inactive state
			sscanf(buf+8,"%2X",&i);
			if ((i>=0)&&(i<max_replica)) {
				disable_replica(-i);
				fprintf(fo,"0002OK");
			} else {
				fprintf(fo,"0003ERR");
			}
			fflush(fo);
			/*
			   } else if (strncmp(buf,"enable:",7)==0) {
			// put replica in partial active state
			// but do not trust it entirely
			sscanf(buf+6,"%2X",&i);
			if ((i>=0)&&(i<max_replica)) {
			enable_replica(-i);
			fprintf(fo,"0002OK");
			} else {
			fprintf(fo,"0003ERR");
			}
			fflush(fo);
			*/
	} else if (strncmp(buf,"trust:",6)==0) {
		// put replica in active state and trust it
		sscanf(buf+6,"%2X",&i);
		if ((i>=0)&&(i<max_replica)) {
			trust_replica(-i);
			fprintf(fo,"0002OK");
		} else {
			fprintf(fo,"0003ERR");
		}
		fflush(fo);
	} else if (strcmp(buf,"info")==0) {
		// get fs info
		fprintf(fo,"%4X%s",(unsigned int)strlen(mount_point),mount_point);
		fflush(fo);
		fprintf(fo,"%4X%s",(unsigned int)strlen(chironctl_mountpoint),chironctl_mountpoint);
		fflush(fo);
		fprintf(fo,"0002%02X",max_replica);
		fflush(fo);
		for(i=0;i<max_replica;++i) {
			fprintf(fo,"%4X%s",(unsigned int)paths[i].pathlen,paths[i].path);
			fflush(fo);
		}
	} else {
		fprintf(fo,"0003ERR");
		fflush(fo);
	}
	}

}

void print_version(void)
{
	printf(
	       "This is the ChironFS V%s, a Fuse based filesystem which implements\n"
	       "filesystem replication.\n",
	       PACKAGE_VERSION
	      );
}


void help(void)
{
	puts("Usage: chironfs [OPTIONS] path=path[=path[=path...]] mount-point");
	print_version();
	puts(
	     "Options:\n"
	     "\t--ctl, -c PATH\n"
	     "\t\tMounts a proc-like filesystem in PATH enabling control operations,\n"
	     "\t\tsee man page and howto to more info about it.\n"
	     "\t--fuseoptions FUSE OPTIONS, -f FUSE OPTIONS\n"
	     "\t\tHere you can set Fuse specific options. See Fuse\n"
	     "\t\tspecific documentation for more help.\n"
	     "\t--help, -h, -?\n"
	     "\t\tPrints this help\n"
	     "\t--log FILE, -l FILE\n"
	     "\t\tSets a log filename. If you don't set it, no log will be\n"
	     "\t\tdone at all\n"
	     "\t--fsname NAME, -n NAME\n"
	     "\t\tDefines the filesystem label name, for viewing it in df or mount\n"
	     "\t--quiet, -q\n"
	     "\t\tDo not print error messages to stderr, just set the exit codes\n"
	     "\t\tThis does not affect logging\n"
	     "\t--version, -V\n"
	     "\t\tPrints version of the software\n"
	     "\n"
	     "path=path[=path[=path...]]\n"
	     "\tThis the '=' separated list of paths where the replicas will be stored\n"
	     "\n"
	     "mount-point\n"
	     "\tThe mount-point through which the replicas will be accessed"
	     );
}

/*
   void printf_args(int argc, char**argv, int ndx)
   {
   int i;
   printf("\n%d\n",ndx);
   for(i=0;i<argc;++i) {
   printf("%s,",argv[i]);
   }
   }
   */

char *chiron_realpath(char *path)  //UPD: Check all
{
	char *slash, *basedir, *realbasedir, *really_realpath, *fname;
	slash = strrchr(path,'/');
	if (slash==NULL) {
		basedir = getcwd(NULL,0);
		if (basedir==NULL) {
			return(NULL);
		}
		fname = path;
	} else {
		(*slash) = 0;
		basedir = strdup(path);
		(*slash) = '/';
		if (basedir==NULL) {
			return(NULL);
		}
		fname = slash + 1;
	}
	if (basedir[0]==0) {
		really_realpath = strdup(fname);
		if (really_realpath==NULL) {
			free(basedir);
			return(NULL);
		}
	} else {
		realbasedir = do_realpath(basedir,NULL);
		if (realbasedir==NULL) {
			free(basedir);
			return(NULL);
		}
		really_realpath = malloc(strlen(realbasedir)+strlen(fname)+2);
		if (really_realpath==NULL) {
			free(basedir);
			free(realbasedir);
			return(NULL);
		}
		sprintf(really_realpath,"%s/%s",realbasedir,fname);
		free(realbasedir);
	}
	free(basedir);
	return(really_realpath);
}

static struct fuse_operations chiron_oper = {
    .destroy      = chiron_destroy,
    .init         = chiron_init,
    .getattr      = chiron_getattr,
    .access       = chiron_access,
    .readlink     = chiron_readlink,
    .readdir      = chiron_readdir,
    .mknod        = chiron_mknod,
    .mkdir        = chiron_mkdir,
    .symlink      = chiron_symlink,
    .unlink       = chiron_unlink,
    .rmdir        = chiron_rmdir,
    .rename       = chiron_rename,
    .link         = chiron_link,
    .chmod        = chiron_chmod,
    .chown        = chiron_chown,
    .truncate     = chiron_truncate,
    .utime        = chiron_utime,
    .open         = chiron_open,
    .read         = chiron_read,
    .write        = chiron_write,
    .statfs       = chiron_statfs,
    .release      = chiron_release,
    .fsync        = chiron_fsync,
    .flush        = chiron_flush,
#ifdef HAVE_SETXATTR
    .setxattr     = chiron_setxattr,
    .getxattr     = chiron_getxattr,
    .listxattr    = chiron_listxattr,
    .removexattr  = chiron_removexattr,
#endif
};


int main(int argc, char *argv[])
{

	int option_index = 0;
	umask(0);

	qtopt = 0;

	dbg("\n-------------------------------------------------------------------------------");
	dbg("\n STARTING ");
	dbg("\n-------------------------------------------------------------------------------");


	do {
		c = getopt_long (argc, argv, short_options, long_options, &option_index);
		switch (c) {
		case 'h':
		case '?':
			help();
			exit(0);
			break;

		case 'V':
			print_version();
			exit(0);
			break;

		case 'n':
			argvbuf = malloc(8+strlen(optarg));
			if (argvbuf==NULL) {
				print_err(CHIRONFS_ERR_LOW_MEMORY,"comand line option (-n) parse buffer allocation");
				exit(CHIRONFS_ERR_LOW_MEMORY);
			}
			sprintf(argvbuf,"fsname=%s", optarg);
			qtopt+=2;
			break;
		case 'f':
			fuse_options = strdup(optarg);
			if (fuse_options==NULL) {
				print_err(CHIRONFS_ERR_LOW_MEMORY,"command line option (-f) argument allocation");
				exit(CHIRONFS_ERR_LOW_MEMORY);
			}
			qtopt+=2;
			break;
		case 'o':
			fuse_options = strdup(optarg);
			if (fuse_options==NULL) {
				print_err(CHIRONFS_ERR_LOW_MEMORY,"comand line option (-o) parse buffer allocation");
				exit(CHIRONFS_ERR_LOW_MEMORY);
			}
			opt_parse(fuse_options,&logname,&argvbuf);
			qtopt+=2;
			break;
		case 'l':
			logname = chiron_realpath(optarg);
			if (logname==NULL) {
				print_err(errno,"comand line option (-l) pathname canonicalization allocation");
				exit(errno);
			}
			qtopt+=2;
			break;
		case 'c':
			mount_ctl = 1;
			chironctl_mountpoint = do_realpath(optarg,NULL);
			if (chironctl_mountpoint==NULL) {
				print_err(CHIRONFS_ERR_LOW_MEMORY,"comand line option (-c) mount point allocation");
				exit(CHIRONFS_ERR_LOW_MEMORY);
			}
			qtopt+=2;
			break;
		case 'q':
			quiet_mode = 1;
			qtopt++;
			break;
		}
	} while (c>=0);

	if ((argc-qtopt)!=3) {
		print_err(CHIRONFS_ERR_BAD_OPTIONS,NULL);
		help();
		exit(CHIRONFS_ERR_BAD_OPTIONS);
	}

	if (logname!=NULL) {
		if (strncmp(argv[qtopt+2],logname,strlen(argv[qtopt+2]))==0) { //UPD: must handle :mountpoint
			print_err(CHIRONFS_ERR_LOG_ON_MOUNTPOINT,NULL);
			exit(CHIRONFS_ERR_LOG_ON_MOUNTPOINT);
		}
	}

	if (argvbuf==NULL) {
		if (argv[qtopt+2][0]==':') {
			argvbuf = malloc(30+strlen(argv[qtopt+1]));
			if (argvbuf!=NULL) {
				//            sprintf(argvbuf,"fsname=%s,direct_io,nonempty", argv[qtopt+1]);
				sprintf(argvbuf,"fsname=%s,nonempty", argv[qtopt+1]);
			}
		} else {
			argvbuf = malloc(20+strlen(argv[qtopt+1]));
			if (argvbuf!=NULL) {
				//            sprintf(argvbuf,"fsname=%s,direct_io", argv[qtopt+1]);
				sprintf(argvbuf,"fsname=%s", argv[qtopt+1]);
			}
		}
	}

	if (argvbuf!=NULL) {

		if (fuse_options!=NULL) {
			fuse_argvlen = strlen(fuse_options);
		}
		fuse_argvlen += strlen(argvbuf);

		fuse_arg = malloc(fuse_argvlen+6);
		if (fuse_arg==NULL) {
			free(argvbuf);
			if (fuse_options!=NULL) {
				free(fuse_options);
			}
			print_err(CHIRONFS_ERR_LOW_MEMORY,"fuse comand line fuse_arg allocation");
			exit(CHIRONFS_ERR_LOW_MEMORY);
		}
		if (fuse_options!=NULL) {
			sprintf(fuse_arg,"-o%s,%s",fuse_options,argvbuf);
		} else {
			sprintf(fuse_arg,"-o%s",argvbuf);
		}

		res = do_mount(argv[qtopt+1],argv[qtopt+2]);

		if (!res) {
			fuse_argv[0] = argv[qtopt+1];
			fuse_argv[1] = mount_point;
			fuse_argv[2] = fuse_arg;

			dbg("\n-------------------------------------------------------------------------------");
			dbg("\nmount: %s %s", argv[qtopt+1], argv[qtopt+2]);
			dbg("\nfuse_argv: %s %s %s", fuse_argv[0], fuse_argv[1], fuse_argv[2]);
			dbg("\n-------------------------------------------------------------------------------");

#ifdef HAVE_GETMNTENT
			FILE *mtab;
			struct mntent *mntentry;

			mtab = setmntent("/etc/mtab", "r");
			do {
				mntentry = getmntent(mtab);
				if (mntentry!=NULL) {
					dbg("\n%s -> %s (%s)", mntentry->mnt_fsname, mntentry->mnt_dir, mntentry->mnt_type);
				}
			} while(mntentry!=NULL);
			endmntent (mtab);
			dbg("\n-------------------------------------------------------------------------------");
#else
			int i, qtent;
#if defined(__FreeBSD__)
			struct statfs *mntbufp;
#else
			struct statvfs *mntbufp;
#endif

			qtent = getmntinfo(&mntbufp, MNT_NOWAIT);
			for(i=0;i<qtent;++i) {
				dbg("%s -> %s (%s)\n",mntbufp[i].f_mntfromname,mntbufp[i].f_mntonname,mntbufp[i].f_fstypename);
			}
#endif

			if (mount_ctl) {
				ctlname = strdup(argv[0]);
				if (ctlname==NULL) {
					free(argvbuf);
					if (fuse_options!=NULL) {
						free(fuse_options);
					}
					free(fuse_arg);
					print_err(-res,"trying to determine the name of the chirctl executable");
					exit(-res);
				}
				sprintf(ctlname+strlen(ctlname)-4,"ctl");
			}

			res = fuse_main(3, fuse_argv, &chiron_oper);
			free(argvbuf);
			if (fuse_options!=NULL) {
				free(fuse_options);
			}
			if (fuse_arg!=NULL) {
				free(fuse_arg);
			}
			//      } else {
			//         printf("\nres:%d",res);
	}

	if (logname!=NULL) {
		free(logname);
	}
	return(res);
	}

	if (fuse_options!=NULL) {
		free(fuse_options);
	}
	print_err(CHIRONFS_ERR_LOW_MEMORY,"fuse comand line argvbuf allocation");
	return(CHIRONFS_ERR_LOW_MEMORY);
}

