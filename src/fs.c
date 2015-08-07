/* Copyright 2005-2008 Luis Furquim
 * Copyright 2015 Thiébaud Weksteen
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

struct chironfs_config config = {
	.max_replica = 0,
	.max_replica_high = 0,
	.max_replica_low = 0,
	.curr_replica_high = 0,
	.curr_replica_low = 0,
	.round_robin_high = NULL,
	.round_robin_low = NULL,
	.replicas = NULL,
	.mountpoint = NULL,
	.chironctl_mountpoint = NULL,
	.chironctl_execname = "chironctl",
	.tab_fd = { NULL, 0 },
	.fd_buf_size = 1
};

struct chironfs_options options;
static struct fuse_opt chironfs_opts[] = {
	CHIRON_OPT("--ctl %s",	       ctl_mountpoint, 0),
	CHIRON_OPT("-c %s",	       ctl_mountpoint, 0),
	CHIRON_OPT("--log %s",	       logname, 0),
	CHIRON_OPT("-l %s",	       logname, 0),
	CHIRON_OPT("--quiet",	       quiet, 1),
	CHIRON_OPT("-q",	       quiet, 1),

	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_KEY("-h",             KEY_HELP),
	FUSE_OPT_KEY("--help",         KEY_HELP),
	FUSE_OPT_END
};


unsigned hash_fd(unsigned fd_main)
{

	return(
	       (config.fd_buf_size > UINT_MAX)
	       ? (hash64shift(fd_main) & (config.fd_buf_size - 1))
	       : (hash(fd_main) & (config.fd_buf_size - 1))
	      );
}

int fd_hashseekfree(unsigned fd_ndx)
{
	unsigned i = fd_ndx;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	dbg("bufsz=%#lx \thash=%#lx\n", config.fd_buf_size, i);

	// If the hash address is used, search for an unused
	// starting at the next address, otherwise use it
	while ((config.tab_fd.fd[i]!=NULL) && (i<config.fd_buf_size)) {
		dbg("X");
		i++;
	}
	if (i<config.fd_buf_size) {
		dbg("used=%#lx\n",i);

		return(i);
	}

	// If there is no free address until the end of the table
	// then restart the search from the beginning of the table
	i = 0;
	while ((config.tab_fd.fd[i]!=NULL) && (i<fd_ndx)) {
		i++;
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("hash time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	if (i<fd_ndx) {
		dbg("\tused=%#lx\n",i);
		return(i);
	}

	// If there is no free address, return the error
	// We expect that it never happens because we set
	// the buffer to hold the file-max (the max opened
	// file system-wide)
	_log("hash allocation","too many opened files",0);
	return(CHIRONFS_ERR_TOO_MANY_FOPENS);
}

int fd_hashset(int *fd)
{
	int i, fd_ndx = -1;

	for(i=0;i<config.max_replica;++i) {
		if ((!config.replicas[i].disabled) && (fd[i]>=0)) {
			dbg("trying %d %d\n", i, fd[i]);
			fd_ndx = fd_hashseekfree(hash_fd(fd[i]));
			break;
		}
	}

	if (i==config.max_replica) {
		return(-1);
	}

	// If the hash address is not used then use it
	if (fd_ndx>=0) {
		config.tab_fd.fd[fd_ndx] = fd;
	}
	return(fd_ndx);
}

char *xlate(const char *fname, char *rpath)
{
	char *rname;
	int   rlen, flen;

	if ((rpath==NULL)||(fname==NULL)) {
		return(NULL);
	}

	if (!strcmp(rpath,".")) {
		flen = strlen(fname);
		rname = malloc(1+flen);
		if (rname!=NULL) {
			if (!strcmp(fname,"/")) {
				strcpy(rname,currdir);
			} else {
				strcpy(rname,fname+1);
			}
		}
	} else {
		rlen = strlen(rpath);
		flen = strlen(fname);
		rname = malloc(1+rlen+flen);
		if (rname!=NULL) {
			strcpy(rname,rpath);
			strcpy(rname+rlen,fname);
		}
	}
	dbg("xlate %s\n",rname);
	return(rname);
}

int choose_replica(int try)
{
	unsigned int i;
	decl_tmvar(t1, t2, t3);
	gettmday(&t1,NULL);

	if (!try) {
		config.curr_replica_high = (config.curr_replica_high + 1) %
			config.max_replica_high;
	}

	if (try == config.max_replica_high) {
		config.curr_replica_low = (config.curr_replica_low + 1) %
			config.max_replica_low;
	}

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("choose_replica time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	if (try < config.max_replica_high) {
		i = (config.curr_replica_high + try) % config.max_replica_high;
		dbg("try=%u curr_replica_high=%u i=%u round_robin_high[i]=%u\n",
		    try, config.curr_replica_high, i,
		    config.round_robin_high[i]);
		return config.round_robin_high[i];
	}
	else {
		i = (config.curr_replica_low + try - config.max_replica_high) % config.max_replica_low;
		dbg("try=%u curr_replica_low=%u i=%u round_robin_low[i]=%u\n",
		    try, config.curr_replica_low, i,
		    config.round_robin_low[i]);
		return config.round_robin_low[i];
	}
}

void disable_replica(int n)
{
	config.replicas[n].disabled = 1;
	_log("disabling replica", config.replicas[n].path, 0);
	dbg("disabling replica %u\n", n);
}

void enable_replica(int n)
{
	config.replicas[n].disabled = 0;
	_log("enabling replica", config.replicas[n].path, 0);
	dbg("enabling replica %u\n", n);
}


/*
 * Disable faulty replicas if and only if there has been at least one 
 * replica which succeeded.  
 */
void disable_faulty_replicas(char *operation, int succ_cnt, int fail_cnt,
			     int *err_list)
{
	int i;

	if (fail_cnt && succ_cnt) {
		for (i = 0; i < config.max_replica; i++) {
			if (config.replicas[i].disabled) {
				continue;
			}
			if (err_list[i]) {
				_log(operation,
				     config.replicas[i].path,
				     err_list[i]);
				disable_replica(i);
			}
		}
	}
}

/* 
 * mount(8): When grpid is set, it takes the group id of the directory in which
 * it is created; otherwise (the default) it takes the fsgid of the current 
 * process, unless the directory has the setgid bit set, in which case it takes
 * the gid from the parent directory, and also gets the setgid bit set if it is 
 * a directory itself.
 *
 * This is a partial implementation of this requirement. Not sure this is
 * necessary...
 */

void fix_gid(char *fname)
{
	char *slash;
	int dir_stat;
	unsigned int gid;
	struct stat stbuf;
	static struct fuse_context *context;

	context = fuse_get_context();
	gid = context->gid;
	slash = strrchr(fname,'/');

	if (slash) {
		if (slash == fname) {
			dir_stat = stat("/", &stbuf);
		} else {
			(*slash) = 0;
			dir_stat = stat(fname, &stbuf);
			(*slash) = '/';
		}
		if (!dir_stat) {
			if (stbuf.st_mode & 02000) {
				dbg("adapting gid from %u to %u mode=%o\n",
				    context->gid, stbuf.st_gid,
				    stbuf.st_mode);
				gid = stbuf.st_gid;
				lchown(fname, context->uid, gid);
			}
		}
	}
}

// Returns permissions from struct stat
int process_rights(struct stat *stbuf)
{
	int res, perm;
	int i = 1024;
	char *buffer;
	struct group  result_buf, *result=NULL;
	struct fuse_context *context = fuse_get_context();
	struct passwd *pw;
	char         **member;
	uid_t  pw_uid;

	//dbg("process_rights cuid=%d cgid=%d stuid=%d sgid=%d\n",
	//    context->uid, context->gid, stbuf->st_uid, stbuf->st_gid);
	if (context->uid == stbuf->st_uid) {
		perm = (stbuf->st_mode & 0700) >> 6;
	} else if (context->gid == stbuf->st_gid) {
		perm = (stbuf->st_mode & 070) >> 3;
	} else {
		do {
			buffer = calloc(i, sizeof(char));
			if (buffer) {
				res = getgrgid_r(stbuf->st_gid,
						 &result_buf,
						 buffer, i,
						 &result);
				if (res==ERANGE) {
					free(buffer);
					i <<= 1;
				}
			}
		} while ((res==ERANGE) && buffer);
		if (!buffer) {
			errno = ENOMEM;
			return -1;
		}
		if (result) {
			member = result->gr_mem;
			while (*member) {
				pw = getpwnam(*member);
				pw_uid = pw->pw_uid;
				if (pw_uid==context->uid) {
					perm = (stbuf->st_mode&070) >> 3;
					break;
				}
				member++;
			}
			if (*member==NULL) {
				perm = stbuf->st_mode&7;
			}
		} else {
			perm = stbuf->st_mode&7;
		}
		free(buffer);
	}
	return perm;
}


// verify if all directories in the path are allowed to enter (rwx, r-x or --x)
int may_enter(const char *filename)
{
	return 0;
	/*
	int perm;
	char *dname, *bkdname;
	struct stat stbuf;

	dbg("may_enter %s\n", filename);
	dname = strdup(filename);
	if (!dname) {
		errno = ENOMEM;
		return -1;
	}
	bkdname = dname;
	do {
		dname = dirname(dname);
		if(stat(dname, &stbuf)) {
			dbg("may_enter stat failed\n");
		}
		perm = process_rights(&stbuf);
		if (!(perm & 1)) {
			free(bkdname);
			errno = EACCES;
			dbg("dirperm: %d\n", perm);
			return -1;
		}
	} while (dname && (strcmp(dname,"/")));

	free(bkdname);
	return 0;*/
}



int get_rights_by_name(const char *fname)
{
	int perm;
	struct stat stbuf;
	struct fuse_context *context = fuse_get_context();

	dbg("get_rights_by_name %s context->uid=%d\n", fname, context->uid);
	if (!context->uid) {
		return 7;
	}
	if (may_enter(fname)) {
		return -1;
	}
	if (stat(fname, &stbuf)) {
		dbg("get_right_by_name stat failed\n");
		return -1;
	}
	perm = process_rights(&stbuf);
	dbg("perm: %d\n",perm);
	return perm;
}


int get_rights_by_name_l(const char *fname)
{
	int perm;
	struct stat stbuf;
	struct fuse_context *context = fuse_get_context();

	dbg("get_rights_by_name_l %s\n", fname);
	if (!context->uid) {
		return 7;
	}
	if (may_enter(fname)) {
		return -1;
	}
	if (lstat(fname, &stbuf)) {
		dbg("get_right_by_name_l stat failed");
		return -1;
	}
	perm = process_rights(&stbuf);
	dbg("perm: %d\n",perm);
	return perm;
}


int get_rights_by_fd(int fd)
{
	int perm;
	struct stat stbuf;
	struct fuse_context *context = fuse_get_context();

	dbg("get_rights_by_fd %d\n", fd);
	if (!context->uid) {
		return 7;
	}
	if (fstat(fd, &stbuf)) {
		dbg("get_right_by_fd stat failed");
		return -1;
	}
	perm = process_rights(&stbuf);
	dbg("perm: %d\n",perm);
	return perm;
}

int get_rights_by_mode(struct stat stbuf)
{
	int perm;
	struct fuse_context *context = fuse_get_context();

	dbg("get_rights_by_mode\n");
	if (!context->uid) {
		return(7);
	}
	perm = process_rights(&stbuf);
	dbg("perm: %d\n",perm);
	return(perm);
}

int check_may_enter(char *fname)
{
	struct fuse_context *context = fuse_get_context();

	dbg("check_may_enter\n");
	if (!context->uid) {
		return(0);
	}
	return may_enter(fname);
}



/*
 * Returns the first error from an error list and log it
 */
int get_first_error(int *err_list)
{
	unsigned int i;
	for(i = 0; i < config.max_replica; i++) {
		if (!config.replicas[i].disabled) {
			if (err_list[i]) {
				dbg("retval: %d %s\n", err_list[i],
				    strerror(-err_list[i]));
				errno = err_list[i];
				return errno;
			}
		}
	}
	return 0;
}

static int chiron_open(const char *path_orig, struct fuse_file_info *fi)
{
	char   *fname, *path, *dname;
	int     i, fd_ndx = -1, *fd, *err_list, perm, perm_mask=0;
	int     succ_cnt=0, fail_cnt=0, file_exists;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);
	decl_tmvar(t4, t5, t6);

	dbg("\n@open %s\n", path_orig);
	gettmday(&t1,NULL);

	fd = calloc(config.max_replica, sizeof(int));
	if (!fd) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(fd);
		return -ENOMEM;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		free(fd);
		free(path);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		gettmday(&t4,NULL);

		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			fail_cnt++;
			err_list[i] = -ENOMEM;
			fd[i] = -1;
			continue;
		}

		file_exists = lstat(fname, &stbuf);
		if (file_exists < 0) {
			file_exists = (errno != ENOENT);
		} else {
			file_exists = 1;
		}
		if (!file_exists && fi->flags & O_CREAT) {
			dname = strdup(fname);
			if (!dname) {
				errno = ENOMEM;
				perm  = -1;
			} else {
				perm = get_rights_by_name(dirname(dname));
			}
		} else {
			perm = get_rights_by_name(fname);
		}
		if (perm < 0) {
			fail_cnt++;
			err_list[i] = -errno;
			fd[i] = -1;
			free(fname);
			continue;
		}

		if (fi->flags & (O_WRONLY|O_TRUNC|O_APPEND|O_CREAT)) {
			perm_mask = 2;
		} else if (fi->flags & O_RDWR) {
			perm_mask = 6;
		} else if ((fi->flags & (O_RDONLY|O_EXCL)) == (O_RDONLY)) {
			perm_mask = 4;
		}
		if (((perm&perm_mask)!=perm_mask) || (!perm_mask) || (!perm)) {
			fail_cnt++;
			err_list[i] = -EACCES;
			dbg("failopen, perm=%d, perm_mask=%d, fi->flags=0%o\n",
			    perm, perm_mask, fi->flags);
			free(fname);
			continue;
		}

		fd[i] = open(fname, fi->flags);
		if (fd[i] < 0) {
			fail_cnt++;
			err_list[i] = -errno;
			free(fname);
			continue;
		}

		if (!file_exists) {
			fix_gid(fname);
			succ_cnt++;
		} else {
			succ_cnt++;
			dbg("opened fd=%x, perm=%d, perm_mask=%d, fi->flags=0%o\n",
			    fd[i],perm,perm_mask,fi->flags);
		}

		free(fname);

		gettmday(&t5,NULL);
		timeval_subtract(&t6,&t5,&t4);
		dbg("open replica time %ld secs, %ld usecs\n",
		    t6.tv_sec,t6.tv_usec);
	}

	free(path);

	/* Partial success, we disable broken replicas */
	disable_faulty_replicas("open", succ_cnt, fail_cnt, err_list);

	dbg("fd = %p [ ", fd);
	for(i = 0; i < config.max_replica; i++)
		dbg("%x ", fd[i]);
	dbg("]\n");

	/* All the replicas failed */
	if (!succ_cnt) {
		for(i = 0; i < config.max_replica; i++) {
			if (!config.replicas[i].disabled) {
				if (err_list[i]) {
					dbg("retval: %d, %d, %d\n",
					    -err_list[i],err_list[i],i);
					errno = err_list[i];
				}
			}
		}
		free(err_list);
		free(fd);
		return errno;
	}

	free(err_list);

	/* Try to allocate a slot inside the file descriptors pool */
	if ((fd_ndx = fd_hashset(fd)) < 0) {
		for(i = 0; i < config.max_replica; i++) {
			if (!config.replicas[i].disabled && fd[i] >= 0) {
				close(fd[i]);
			}
		}
		free(fd);
		return -EMFILE;
	}
	fi->fh = fd_ndx;

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("open total time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}

static int chiron_release(const char *path, struct fuse_file_info *fi)
{
	int i, r;
	decl_tmvar(t1, t2, t3);

	dbg("\n@release %#lx\n",fi->fh);
	gettmday(&t1,NULL);

	if (!config.tab_fd.fd[fi->fh]) {
		return -EINVAL;
	}

	for(i = 0, r = -1; i < config.max_replica; i++) {
		if (config.tab_fd.fd[fi->fh][i] > 0 &&
		    !config.replicas[i].disabled) {
			r &= close(config.tab_fd.fd[fi->fh][i]);
		}
	}
	free(config.tab_fd.fd[fi->fh]);
	config.tab_fd.fd[fi->fh] = NULL;

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("release time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	return r;
}

static int chiron_read(const char *path, char *buf, size_t size,
		       off_t offset, struct fuse_file_info *fi)
{
	int     i, replica, *err_list, perm, err;
	ssize_t r;
	int     fail_cnt=0, succ_cnt=0;
	decl_tmvar(t1, t2, t3);

	dbg("\n@read %x\n", fi->fh);
	gettmday(&t1,NULL);

	if (!config.tab_fd.fd[fi->fh]) {
		return -EINVAL;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		replica = choose_replica(i);
		if (config.tab_fd.fd[fi->fh][replica] < 0 ||
		    config.replicas[replica].disabled) {
			continue;
		}
		perm = get_rights_by_fd(config.tab_fd.fd[fi->fh][replica]);
		if (perm < 0) {
			err_list[replica] = -errno;
			fail_cnt++;
			continue;
		}
		if (! (perm & 4)) {
			err_list[replica] = -EACCES;
			fail_cnt++;
			continue;
		}
		r = pread(config.tab_fd.fd[fi->fh][replica], buf, size, offset);
		if (r < 0) {
			err_list[replica] = errno;
			fail_cnt++;
			continue;
		}
		succ_cnt++;
		break;
	}

	disable_faulty_replicas("read", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("read time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	return r;
}

static int chiron_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	int	i, fail_cnt=0, succ_cnt=0, *err_list, err, perm;
	ssize_t w, w_max=0;
	decl_tmvar(t1, t2, t3);

	dbg("\n@write %#lx\n", fi->fh);
	gettmday(&t1,NULL);

	if (!config.tab_fd.fd[fi->fh]) {
		return -EINVAL;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled ||
		    config.tab_fd.fd[fi->fh][i] < 0) {
			continue;
		}

		/*
		perm = get_rights_by_fd(config.tab_fd.fd[fi->fh][i]);
		if (perm < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			continue;
		}
		if (!(perm & 2)) {
			err_list[i] = -EACCES;
			fail_cnt++;
			continue;
		}*/
		w = pwrite(config.tab_fd.fd[fi->fh][i], buf, size, offset);
		if (w < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			continue;
		}
		w_max = max(w, w_max);
		succ_cnt++;

	}

	disable_faulty_replicas("write", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("write total time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	return w_max;
}


int chiron_getattr(const char *path, struct stat *stbuf)
{
	char *fname;
	int i=0, st=-1, replica;
	int succ_cnt=0, fail_cnt=0;
	int *err_list, err, perm;
	decl_tmvar(t1, t2, t3);

	dbg("\n@getattr %s\n", path);
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		replica = choose_replica(i);
		if (config.replicas[replica].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[replica].path);
		if(!fname) {
			continue;
		}

		perm = check_may_enter(fname);
		if (perm < 0) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		st = lstat(fname, stbuf);
		if (st) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		succ_cnt++;
		free(fname);
		break;
	}

	disable_faulty_replicas("getattr", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("getattr total time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}


static int chiron_access(const char *path, int mask)
{
	int ret, perm;
	struct stat stbuf;

	dbg("\n@access %s\n",path);
	ret = chiron_getattr(path,&stbuf);

	if (ret < 0) {
		return ret;
	}

	perm = get_rights_by_mode(stbuf);
	if (perm >= 0) {
		if ((perm & mask) == mask) {
			return 0;
		}
		return -EACCES;
	}
	return perm;
}


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
	char *fname;
	int i=0, st=-1, replica;
	int succ_cnt=0, fail_cnt=0;
	int *err_list, err, perm;
	decl_tmvar(t1, t2, t3);

	dbg("\n@readlink %s\n",path);
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		replica = choose_replica(i);
		if (config.replicas[replica].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[replica].path);
		if (!fname) {
			continue;
		}

		perm = get_rights_by_name_l(fname);
		if (perm < 0) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		if (!(perm & 4)) {
			err_list[replica] = -EACCES;
			fail_cnt++;
			free(fname);
			continue;
		}

		st = readlink(fname, buf, size);
		if (st == -1) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		succ_cnt++;
		buf[st] = 0;
		free(fname);
		break;
	}

	disable_faulty_replicas("readlink", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2, &t1);
	dbg("readlink time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	return 0;
}


static int chiron_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	char *fname;
	int            i=0, replica, perm;
	int succ_cnt=0, fail_cnt=0;
	DIR           *dp = NULL;
	struct dirent *de;
	int           *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@readdir %s offset=%ld\n",path,offset);
	gettmday(&t1, NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		replica = choose_replica(i);
		if (config.replicas[replica].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[replica].path);
		if (!fname) {
			continue;
		}

		perm = get_rights_by_name(fname);
		if (perm < 0) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		if (!(perm & 4)) {
			err_list[replica] = -EACCES;
			fail_cnt++;
			free(fname);
			continue;
		}

		dp = opendir(fname);
		if (!dp) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		succ_cnt++;
		free(fname);
		seekdir(dp, offset);
		while ((de = readdir(dp)) != NULL) {
			if (filler(buf, de->d_name, NULL, 0))
				break;
		}
		closedir(dp);
		break;
	}


	disable_faulty_replicas("readdir", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2 ,&t1);
	dbg("readdir time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}

static int chiron_mknod(const char *path_orig, mode_t mode, dev_t rdev)
{
	char   *fname, *path, *dname;
	int     i, fd, res, succ_cnt=0, fail_cnt=0, *err_list, err, perm;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);
	dbg("\n@mknod %s\n", path_orig);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(err_list);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		dname = strdup(fname);
		if (!dname) {
			fail_cnt++;
			err_list[i] = -ENOMEM;
			free(fname);
			continue;
		}

		perm = get_rights_by_name(dirname(dname));
		free(dname);

		if (perm < 0) {
			fail_cnt++;
			err_list[i] = -errno;
			free(fname);
			continue;
		}

		if (!(perm & 2)) {
			fail_cnt++;
			err_list[i] = -EACCES;
			free(fname);
			continue;
		}

		if (S_ISREG(mode)) {
			fd = open(fname, O_CREAT | O_EXCL | O_WRONLY, mode);
			if (fd < 0) {
				err_list[i] = -errno;
				fail_cnt++;
				free(fname);
				continue;
			}
			close(fd);
			fix_gid(fname);
			succ_cnt++;
			dbg("mknod/open+chown: %s\n", path_orig);

		} else if (S_ISFIFO(mode)) {
			res = mkfifo(fname, mode);
			if (res) {
				err_list[i] = -errno;
				fail_cnt++;
				free(fname);
				continue;
			}
			fix_gid(fname);
			succ_cnt++;
			dbg("mknod/fifo+chown: %s\n",path_orig);

		} else {
			free(fname);
			return -ENOSYS;
		}
	}
	free(path);

	disable_faulty_replicas("mknod", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2 ,&t1);
	dbg("mknod time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}


#define do_byname_rw(fn,logmsgstr,perm_check,permop,restrict,nerrno)                           \
	char   *fname, *path;                                                                       \
int     i, *fd;                                                                             \
int     fail_cnt=0, succ_cnt=0;                                                             \
int    *err_list, perm;                                                                     \
decl_tmvar(t1, t2, t3);                                                                     \
gettmday(&t1,NULL);                                                                         \
fd = calloc(config.max_replica,sizeof(int));                                                       \
if (fd==NULL) {                                                                             \
	return -ENOMEM;                                                                          \
}                                                                                           \
err_list = calloc(config.max_replica,sizeof(int));                                                 \
if (err_list==NULL) {                                                                       \
	free(fd);                                                                                \
	return -ENOMEM;                                                                          \
}                                                                                           \
path = strdup(path_orig);                                                                   \
if (path!=NULL) {                                                                           \
	for(i=(config.max_replica-1);i>=0;--i) {                                                        \
		if (!config.replicas[i].disabled) {                                                             \
			fname = xlate(path,config.replicas[i].path);                                                 \
			if (fname!=NULL) {                                                                 \
				perm = perm_check;                                                              \
				if (perm<0) {                                                                   \
					err_list[i] = errno;                                                         \
					fail_cnt++;                                                                  \
					dbg("%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);          \
				} else if ((!(perm&2)) permop (restrict)) {                                     \
					err_list[i] = nerrno;                                                        \
					fail_cnt++;                                                                  \
					dbg("%s try %d, errno: %d, perm: %x\n",logmsgstr,i,nerrno,perm);         \
				} else {                                                                        \
					fd[i] = fn;                                                                  \
					free(fname);                                                                 \
					if (fd[i]==0) {                                                              \
						succ_cnt++;                                                               \
						err_list[i] = 0;                                                          \
						dbg("%s succ %d, perm: %x\n",logmsgstr,i,perm);                          \
					} else {                                                                     \
						err_list[i] = errno;                                                      \
						fail_cnt++;                                                               \
						dbg("%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);          \
					}                                                                            \
				}                                                                               \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	free(path);                                                                              \
	if (fail_cnt && succ_cnt) {                                                              \
		for(i=0;i<config.max_replica;++i) {                                                          \
			if (!config.replicas[i].disabled) {                                                          \
				if (fd[i]<0) {                                                                  \
					_log(logmsgstr,config.replicas[i].path,err_list[i]);                               \
					disable_replica(i);                                                          \
				}                                                                               \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	for(i=(config.max_replica-1);i>=0;--i) {                                                        \
		if (!config.replicas[i].disabled) {                                                             \
			if (err_list[i]) {                                                                 \
				dbg("retval: %d,%d,%d\n",-err_list[i],err_list[i],i);                       \
				return(-err_list[i]);                                                           \
			}                                                                                  \
		}                                                                                     \
	}                                                                                        \
	free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */ \
	free( err_list );                                                                        \
	gettmday(&t2,NULL);                                                                      \
	timeval_subtract(&t3,&t2,&t1);                                                           \
	dbg("%s time %ld secs, %ld usecs\n",logmsgstr,t3.tv_sec,t3.tv_usec);                  \
	if (succ_cnt) {                                                                          \
		return(0);                                                                            \
	}                                                                                        \
	return -errno;                                                                           \
}                                                                                           \
free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */    \
free( err_list );                                                                           \
gettmday(&t2,NULL);                                                                         \
timeval_subtract(&t3,&t2,&t1);                                                              \
dbg("%s allocfail time %ld secs, %ld usecs\n",logmsgstr,t3.tv_sec,t3.tv_usec);           \
return -ENOMEM


static int chiron_truncate(const char *path_orig, off_t size)
{
	//do_byname_rw(truncate(fname, size), "truncate",get_rights_by_name(fname),||,0,EACCES);
	char   *fname, *path;
	int     i, fd;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err, perm;
	decl_tmvar(t1, t2, t3);

	dbg("\n@truncate %s\n", path_orig); //str
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(err_list);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		perm = get_rights_by_name(fname); //perm_check
		if (perm < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		if (!(perm & 2)) { // permopm, restrict
			err_list[i] = -EACCES; //nerrno
			fail_cnt++;
			continue;
		}

		fd = truncate(fname, size); //fn
		free(fname);

		if (fd < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
	}
	free(path);

	disable_faulty_replicas("truncate", succ_cnt, fail_cnt, err_list); //str

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("truncate time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec); //str

	return 0;
}

static int chiron_chmod(const char *path_orig, mode_t mode)
{
	char   *fname, *path;
	int     i, fd, fail_cnt=0, succ_cnt=0;
	int    *err_list, err, res;
	struct stat stbuf;
	struct fuse_context *context;
	decl_tmvar(t1, t2, t3);

	//do_byname_rw(chmod(fname, mode), "chmod",2,||,(
	//					       // deny if user is not privileged neither the owner
	//					       (context->uid && (stbuf.st_uid!=context->uid))
	//					      ),EPERM);

	dbg("\n@chmod %s %o\n", path_orig, mode);
	gettmday(&t1,NULL);

	context = fuse_get_context();
	if ((res = chiron_getattr(path_orig, &stbuf)) < 0) {
		return res;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(err_list);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		if (context->uid && (stbuf.st_uid!=context->uid)) {
			err_list[i] = -EPERM;
			fail_cnt++;
			continue;
		}

		fd = chmod(fname, mode);
		free(fname);

		if (fd < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
	}
	free(path);

	disable_faulty_replicas("chmod", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("chmod time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;

}

static int chiron_chown(const char *path_orig, uid_t uid, gid_t gid)
{
	char   *fname, *path;
	int     i, fd, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err, perm;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);

	dbg("\n@chown %s uid=%u gid=%u\n", path_orig, uid, gid);
	gettmday(&t1,NULL);

	context = fuse_get_context();
	if ((res = chiron_getattr(path_orig, &stbuf)) < 0) {
		return res;
	}
	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(err_list);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		perm = get_rights_by_name(fname);
		if (perm < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		if (!(perm & 2) ||
		    // deny if system is restricted, user is not privileged and is trying to change the owner
		    (_POSIX_CHOWN_RESTRICTED && context->uid && (stbuf.st_uid!=uid)) ||
		    // deny if user is not privileged neither the owner and is trying to change the owner
		    (context->uid && (stbuf.st_uid!=context->uid) && (stbuf.st_uid!=uid) ) ||
		    // deny if user is not privileged neither the owner and is trying to change the group
		    (context->uid && (stbuf.st_uid!=context->uid) && (stbuf.st_gid!=gid))) { 
			err_list[i] = -EPERM;
			fail_cnt++;
			continue;
		}

		fd = lchown(fname, uid, gid);
		free(fname);

		if (fd < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
	}
	free(path);

	disable_faulty_replicas("chown", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("chown time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}

static int chiron_utime(const char *path_orig, struct utimbuf *buf)
{
	char   *fname, *path;
	int     i, fd, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err, perm;
	static struct fuse_context *context;
	struct stat stbuf;
	decl_tmvar(t1, t2, t3);

	dbg("\n@utime %s\n", path_orig);
	gettmday(&t1,NULL);

	context = fuse_get_context();
	if ((res = chiron_getattr(path_orig, &stbuf)) < 0) {
		return res;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		return -ENOMEM;
	}

	path = strdup(path_orig);
	if (!path) {
		free(err_list);
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		perm = get_rights_by_name(fname);
		if (perm < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		if (!buf && !(perm & 2 || context->uid ||
			      stbuf.st_uid == context->uid)) {
			err_list[i] = -EACCES;
			fail_cnt++;
			continue;
		}

		if (buf && !(context->uid || (stbuf.st_uid == context->uid))) {
			err_list[i] = -EPERM;
			fail_cnt++;
			continue;
		}

		fd = utime(fname, buf);
		free(fname);

		if (fd < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
	}
	free(path);

	disable_faulty_replicas("utime", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("utime time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	return 0;
}

static int chiron_rmdir(const char *path_orig)
{
	char *dname;
	int tmpperm;
	dbg("\n@rmdir %s\n",path_orig);
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
	dbg("\n@unlink %s\n",path_orig);
	do_byname_rw(unlink(fname), "unlink",(
					      ((dname=strdup(fname))==NULL)
					      ? ( errno = ENOMEM, -1 )
					      : ( tmpperm = get_rights_by_name(dirname(dname)), free(dname), tmpperm)
					     ),||,0,EACCES);
}

int chiron_mkdir(const char *path_orig, mode_t mode)
{
	char   *fname, *path, *dname;
	int     i, *fd;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, perm;
	dbg("\n@mkdir %s\n",path_orig);

	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	fd = calloc(config.max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}
	err_list = calloc(config.max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}
	path = strdup(path_orig);
	if (path!=NULL) {
		for(i=(config.max_replica-1);i>=0;--i) {
			if (!config.replicas[i].disabled) {
				fname = xlate(path,config.replicas[i].path);
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
							fix_gid(fname);
							succ_cnt++;
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
			for(i=0;i<config.max_replica;++i) {
				if (!config.replicas[i].disabled) {
					if (fd[i]<0) {
						if (err_list[i]<0) {
							_log("mkdir+chown",config.replicas[i].path,-err_list[i]);
						} else {
							_log("mkdir",config.replicas[i].path,err_list[i]);
						}
						disable_replica(i);
					}
				}
			}
		}

		gettmday(&t2,NULL);
		timeval_subtract(&t3,&t2,&t1);
		dbg("mkdir time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

		if (succ_cnt) {
			free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
			free( err_list );
			return(0);
		}
		for(i=(config.max_replica-1);i>=0;--i) {
			if (!config.replicas[i].disabled) {
				if (err_list[i]) {
					dbg("retval: %d,%d,%d\n",-err_list[i],err_list[i],i);
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
	dbg("mkdir alloc fail time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	return -errno;

}

static int chiron_symlink(const char *from, const char *to)
{
	char *fname, *dname;
	int   i, *fd, fail_cnt=0, succ_cnt=0, *err_list, perm;
	decl_tmvar(t1, t2, t3);

	gettmday(&t1,NULL);

	dbg("\n@symlink %s->%s\n", from, to);
	fd = calloc(config.max_replica,sizeof(int));
	if (fd==NULL) {
		return -ENOMEM;
	}

	err_list = calloc(config.max_replica,sizeof(int));
	if (err_list==NULL) {
		free(fd);
		return -ENOMEM;
	}

	for(i=(config.max_replica-1);i>=0;--i) {
		if (!config.replicas[i].disabled) {
			fd[i] = 0;
			fname = xlate(to,config.replicas[i].path);
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
						fix_gid(fname);
						succ_cnt++;
					}
				}
				free(fname);
			}
		}
	}

	if (fail_cnt && succ_cnt) {
		for(i=0;i<config.max_replica;++i) {
			if (!config.replicas[i].disabled) {
				if (fd[i]<0) {
					if (err_list[i]<0) {
						_log("symlink+chown",config.replicas[i].path,-err_list[i]);
					} else {
						_log("symlink",config.replicas[i].path,err_list[i]);
					}
					disable_replica(i);
				}
			}
		}
	}

	for(i=(config.max_replica-1);i>=0;--i) {
		if (!config.replicas[i].disabled) {
			if (err_list[i]) {
				dbg("retval: %d,%d,%d\n",-err_list[i],err_list[i],i);
				errno = err_list[i];
			}
		}
	}

	free( fd );         /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("symlink time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

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
fd = calloc(config.max_replica,sizeof(int));                                   \
if (fd==NULL) {                                                         \
	return -ENOMEM;                                                      \
}                                                                       \
for(i=(config.max_replica-1);i>=0;--i) {                                       \
	if (!config.replicas[i].disabled) {                                            \
		fd[i] = 0;                                                        \
		fname_from = xlate(from,config.replicas[i].path);                           \
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
			dbg("1. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
		} else if (((perm&perm_from)!=perm_from) && perm_from) {          \
			fd[i] = -EACCES;                                               \
			fail_cnt++;                                                    \
			dbg("1. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,EACCES,perm);           \
		} else {                                                          \
			fname_to = xlate(to,config.replicas[i].path);                            \
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
				dbg("2. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
			} else if ((perm&3)!=3) {                                        \
				fd[i] = -EACCES;                                            \
				fail_cnt++;                                                 \
				dbg("2. %s try %d, errno: %d, perm: %x\n",logmsgstr,i,EACCES,perm);           \
			} else {                                                       \
				fd[i] = fn;                                                 \
				if (fd[i]==0) {                                             \
					succ_cnt++;                                              \
					dbg("%s succ %d, perm: %x\n",logmsgstr,i,perm);                            \
				} else {                                                    \
					fd[i] = -errno;                                          \
					fail_cnt++;                                              \
					dbg("%s try %d, errno: %d, perm: %x\n",logmsgstr,i,errno,perm);            \
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
	for(i=0;i<config.max_replica;++i) {                                         \
		if (!config.replicas[i].disabled) {                                         \
			if (fd[i]<0) {                                                 \
				_log(logmsgstr,config.replicas[i].path,-fd[i]);                   \
				disable_replica(i);                                         \
			}                                                              \
		}                                                                 \
	}                                                                    \
}                                                                       \
gettmday(&t2,NULL);                                                     \
timeval_subtract(&t3,&t2,&t1);                                          \
dbg("%s time %ld secs, %ld usecs\n",logmsgstr,t3.tv_sec,t3.tv_usec); \
if (succ_cnt) {                                                         \
	free( fd ); /* Thanks to Patrick Prasse for send this patch line fixing a memory leak */  \
	return(0);                                                                                \
}                                                                                            \
for(i=(config.max_replica-1);i>=0;--i) {                                                            \
	if (!config.replicas[i].disabled) {                                                                 \
		if (fd[i]) {                                                                           \
			dbg("retval: %d,%d,%d\n",-fd[i],fd[i],i);                                       \
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
	dbg("\n@rename %s->%s mode=%o dir=%d\n",from, to, stbuf.st_mode,S_ISDIR(stbuf.st_mode));
	if (S_ISDIR(stbuf.st_mode)) {
		dbg("rename: %s isdir\n",from);
		dperm = get_rights_by_mode(stbuf);
		if (dperm<0) {
			return(-errno);
		}
		dbg("rename: fromperm=%x\n",dperm);
		if (!(dperm&2)) {
			return(-EACCES);
		}
	}

	if ((st=chiron_getattr(to, &stbuf))>=0) {
		if (S_ISDIR(stbuf.st_mode)) {
			dbg("rename: %s isdir\n",to);
			dperm = get_rights_by_mode(stbuf);
			if (dperm<0) {
				return(dperm);
			}
			dbg("rename: toperm=%x\n",dperm);
			if (!(dperm&2)) {
				return(-EACCES);
			}
		}
	}
	do_by2names_rw(3,rename(fname_from, fname_to), "rename");
}

static int chiron_link(const char *from, const char *to)
{
	dbg("\n@link %s->%s\n", from, to);
	do_by2names_rw(0,link(fname_from, fname_to), "link");
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

	if (config.chironctl_mountpoint) {
		dbg("\nctl: struct stat initialized");
		i = stat(config.chironctl_mountpoint,&st);
		dbg("\nctl: getattr=%d",i);
		if (i==ENOENT) {
			dbg("\nctl: NOENT");
			i = mkdir(config.chironctl_mountpoint,
				  S_IFDIR | S_IRUSR | S_IXUSR);
			dbg("\nctl: mkdir=%d",i);
		}

		if (i) {
			dbg("\nctl: nomkdir");
			_log("control directory", config.chironctl_mountpoint, i);
				return(NULL);
		}

		if (pthread_create(&thand,NULL,start_ctl, NULL) != 0) {
			// must end program here
			return(NULL);
		}
	}
	return NULL;
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
	return 0;
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
		dbg("\nchild: execing %s\n",config.chironctl_execname);
		execlp(config.chironctl_execname,config.chironctl_execname,NULL);
		dbg("\nchild: failed execing %s error is %d\n",config.chironctl_execname,errno);
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
			for(i=0;i<config.max_replica;++i) {
				sscanf(buf+5,"%02X",&i);
				if ((i>=0)&&(i<config.max_replica)) {
					fprintf(fo,"0001%1X",((int)config.replicas[i].disabled));
					fflush(fo);
					break;
				}
			}
		} else if (strncmp(buf,"disable:",8)==0) {
			// put replica in inactive state
			sscanf(buf+8,"%2X",&i);
			if ((i>=0)&&(i<config.max_replica)) {
				disable_replica(-i);
				fprintf(fo,"0002OK");
			} else {
				fprintf(fo,"0003ERR");
			}
			fflush(fo);
		} else if (strcmp(buf,"info")==0) {
			// get fs info
			fprintf(fo,"%4X%s",(unsigned int)strlen(config.mountpoint),config.mountpoint);
			fflush(fo);
			fprintf(fo,"%4X%s",(unsigned int)strlen(config.chironctl_mountpoint),config.chironctl_mountpoint);
			fflush(fo);
			fprintf(fo,"0002%02X",config.max_replica);
			fflush(fo);
			for(i=0;i<config.max_replica;++i) {
				fprintf(fo,"%4X%s",(unsigned int)config.replicas[i].pathlen,config.replicas[i].path);
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
	printf("chironFS version: %s\n", PACKAGE_VERSION);
}


void help(void)
{
	fprintf(stderr, "usage: chironfs [OPTIONS] path=path[=path[=path...]] mountpoint\n");
	fprintf(stderr, "Arguments:\n"
	     "    path=path[=path[=path...]]\n"
	     "        This is the '=' separated list of paths where the replicas will\n"
	     "        be stored. A path starting by ':' will have a low priority\n"
	     "    mountpoint\n"
	     "        The mountpoint through which the replicas will be accessed\n"
	     "\n"
	     "ChironFS options:\n"
	     "    -c PATH, --ctl PATH\n"
	     "        Mounts a proc-like filesystem in PATH enabling control operations\n"
	     "    -h, --help            print this help\n"
	     "    -l FILE, --log file   set a log filename\n"
	     "    -q, --quiet           do not print error messages\n"
	     "    -V, --version         print version of the software\n"
	     "\n"
	     );
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
    .statfs       = NULL,
    .release      = chiron_release,
    .fsync        = NULL,
    .flush        = chiron_flush,
#ifdef HAVE_SETXATTR
    .setxattr     = chiron_setxattr,
    .getxattr     = chiron_getxattr,
    .listxattr    = chiron_listxattr,
    .removexattr  = chiron_removexattr,
#endif
};


static int chironfs_opt_proc(void *data, const char *arg, int key,
			     struct fuse_args *outargs)
{
	switch(key) {
	case FUSE_OPT_KEY_OPT:
		return 1;
	case FUSE_OPT_KEY_NONOPT:
		if (!options.replica_args && strchr(arg, '=')) {
			options.replica_args = strdup(arg);
			return 0;
		} else if (!options.mountpoint) {
			char mountpoint[PATH_MAX];
			if (!realpath(arg, mountpoint)) {
				fprintf(stderr, "fuse: bad mount point `%s': %s\n",
					arg, strerror(errno));
				return -1;
			}
			options.mountpoint = strdup(mountpoint);
			return fuse_opt_add_arg(outargs, mountpoint);
		}
		return 1;
	case KEY_HELP:
		help();
		fuse_opt_add_arg(outargs, "-ho");
		fuse_main(outargs->argc, outargs->argv, &chiron_oper);
		exit(1);
	case KEY_VERSION:
		print_version();
		fuse_opt_add_arg(outargs, "--version");
		fuse_main(outargs->argc, outargs->argv, &chiron_oper);
		exit(1);
	default:
		dbg("Unknown option: %d", key);
		return 0;
	}
}

int main(int argc, char *argv[])
{
	int i, res;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	umask(0);

	dbg("Starting\n");

	if(fuse_opt_parse(&args, &options, chironfs_opts,
			  chironfs_opt_proc) == -1)
		exit(1);

	//fuse_opt_add_arg(&args, "-o");
	//fuse_opt_add_arg(&args, "default_permissions");
	dbg("args.argv =  [\n");
	for(i=0; i < args.argc; i++)
	{
		dbg("\t[%d] [%s]\n", i, args.argv[i]);
	}
	dbg("]\n");
	dbg("options.replica_args = %s\n", options.replica_args);
	dbg("options.mountpoint = %s\n", options.mountpoint);
	dbg("options.logname = %s\n", options.logname);
	dbg("options.ctl_mountpoint = %s\n", options.ctl_mountpoint);

	if(!options.replica_args) {
		print_err(EINVAL, "Replica cannot be empty");
		exit(1);
	}

	if(options.mountpoint) {
		config.mountpoint = options.mountpoint;
	}
	else {
		print_err(EINVAL, "Mountpoint cannot be empty");
		exit(1);
	}

	if(options.logname) {
		open_log(options.logname);
	}

	//mount_ctl = 1;
	//config.chironctl_mountpoint = do_realpath(optarg,NULL);

	do_mount(options.replica_args, options.mountpoint);
	res = fuse_main(args.argc, args.argv, &chiron_oper);

	return res;
}

