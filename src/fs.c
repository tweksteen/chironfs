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
	.uid = 0,
	.gid = 0,
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

	if (!rpath || !fname) {
		return NULL;
	}
	if (!strcmp(rpath, ".")) {
		flen = strlen(fname);
		rname = malloc(1+flen);
		if (rname) {
			if (!strcmp(fname,"/")) {
				strcpy(rname,currdir);
			} else {
				strcpy(rname,fname+1);
			}
		}
	} else {
		rlen = strlen(rpath);
		flen = strlen(fname);
		rname = malloc(1 + rlen + flen);
		if (rname) {
			strcpy(rname, rpath);
			strcpy(rname + rlen,fname);
		}
	}
	dbg("xlate %s\n",rname);
	return rname;
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

int drop_priv(void)
{
	struct fuse_context *context;
	context = fuse_get_context();

	dbg("dropping priv from %u to %u (geteuid=%u)\n", config.uid,
	    context->uid, geteuid());
	dbg("dropping priv from %u to %u (getegid=%u)\n", config.gid,
	    context->gid, getegid());
	if (setegid(context->gid) == -1) {
		dbg("Unable to change effective GID!\n");
		dbg("context->gid=%u %s\n", context->gid, strerror(errno));
		return -1;
	}
	if (seteuid(context->uid) == -1) {
		dbg("Unable to change effective UID!\n");
		dbg("context->uid=%u\n", context->uid);
		return -1;
	}

	return 0;
}

int reacquire_priv(void)
{
	dbg("reacquiring priv from %u to %u\n", getuid(), config.uid);
	if(seteuid(config.uid) == -1 ||
	   setegid(config.gid) == -1) {
		dbg("Unable to change effective UID!\n");
		dbg("config.uid=%u config.gid=%u\n",
		    config.uid, config.gid);
		return -1;
	}
	return 0;
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
				return err_list[i];
			}
		}
	}
	return 0;
}

static int chiron_open(const char *path, struct fuse_file_info *fi)
{
	char   *fname;
	int     i, fd_ndx = -1, *fd, *err_list, err;
	int     succ_cnt=0, fail_cnt=0;
	decl_tmvar(t1, t2, t3);
	decl_tmvar(t4, t5, t6);

	dbg("\n@open %s\n", path);
	drop_priv();
	gettmday(&t1,NULL);

	fd = calloc(config.max_replica, sizeof(int));
	if (!fd) {
		reacquire_priv();
		return -ENOMEM;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		free(fd);
		reacquire_priv();
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

		fd[i] = open(fname, fi->flags);
		if (fd[i] < 0) {
			fail_cnt++;
			err_list[i] = -errno;
			free(fname);
			continue;
		}

		succ_cnt++;
		free(fname);
		dbg("opened fd=%x, fi->flags=0%o\n", fd[i], fi->flags);

		gettmday(&t5,NULL);
		timeval_subtract(&t6,&t5,&t4);
		dbg("open replica time %ld secs, %ld usecs\n",
		    t6.tv_sec,t6.tv_usec);
	}

	/* Partial success, we disable broken replicas */
	disable_faulty_replicas("open", succ_cnt, fail_cnt, err_list);

	dbg("fd = %p [ ", fd);
	for(i = 0; i < config.max_replica; i++)
		dbg("%x ", fd[i]);
	dbg("]\n");

	/* All the replicas failed */
	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		free(fd);
		reacquire_priv();
		return err;
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
		reacquire_priv();
		return -EMFILE;
	}
	fi->fh = fd_ndx;

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("open total time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_release(const char *path, struct fuse_file_info *fi)
{
	int i, r;
	decl_tmvar(t1, t2, t3);

	dbg("\n@release %#lx\n",fi->fh);
	gettmday(&t1,NULL);
	drop_priv();

	if (!config.tab_fd.fd[fi->fh]) {
		reacquire_priv();
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

	reacquire_priv();
	return r;
}

static int chiron_read(const char *path, char *buf, size_t size,
		       off_t offset, struct fuse_file_info *fi)
{
	int     i, replica, *err_list, err;
	ssize_t r;
	int     fail_cnt=0, succ_cnt=0;
	decl_tmvar(t1, t2, t3);

	dbg("\n@read %x\n", fi->fh);
	drop_priv();
	gettmday(&t1,NULL);

	if (!config.tab_fd.fd[fi->fh]) {
		reacquire_priv();
		return -EINVAL;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		replica = choose_replica(i);
		if (config.tab_fd.fd[fi->fh][replica] < 0 ||
		    config.replicas[replica].disabled) {
			continue;
		}

		r = pread(config.tab_fd.fd[fi->fh][replica], buf, size, offset);
		if (r < 0) {
			err_list[replica] = -errno;
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
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("read time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	reacquire_priv();
	return r;
}

static int chiron_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	int	i, fail_cnt=0, succ_cnt=0, *err_list, err;
	ssize_t w, w_max=0;
	decl_tmvar(t1, t2, t3);

	dbg("\n@write %#lx\n", fi->fh);
	drop_priv();
	gettmday(&t1,NULL);

	if (!config.tab_fd.fd[fi->fh]) {
		reacquire_priv();
		return -EINVAL;
	}

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled ||
		    config.tab_fd.fd[fi->fh][i] < 0) {
			continue;
		}

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
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("write total time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	reacquire_priv();
	return w_max;
}


static int chiron_getattr(const char *path, struct stat *stbuf)
{
	char *fname;
	int i=0, res, replica;
	int succ_cnt=0, fail_cnt=0;
	int *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@getattr %s\n", path);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
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

		res = lstat(fname, stbuf);
		if (res) {
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
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("getattr total time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_access(const char *path, int mask)
{
	char *fname;
	int i=0, res, replica;
	int succ_cnt=0, fail_cnt=0;
	int *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@access %s\n", path);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
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

		res = access(fname, mask);
		if (res) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		succ_cnt++;
		free(fname);
		break;
	}

	disable_faulty_replicas("access", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("access total time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
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
	int i=0, res, replica;
	int succ_cnt=0, fail_cnt=0;
	int *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@readlink %s\n",path);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
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

		res = readlink(fname, buf, size);
		if (res == -1) {
			err_list[replica] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}

		succ_cnt++;
		free(fname);
		buf[res] = 0;
		break;
	}

	disable_faulty_replicas("readlink", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2, &t1);
	dbg("readlink time %ld secs, %ld usecs\n",t3.tv_sec,t3.tv_usec);

	reacquire_priv();
	return 0;
}


static int chiron_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	char *fname;
	int            i=0, replica;
	int succ_cnt=0, fail_cnt=0;
	DIR           *dp = NULL;
	struct dirent *de;
	int           *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@readdir %s offset=%ld\n",path,offset);
	drop_priv();
	gettmday(&t1, NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
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
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2 ,&t1);
	dbg("readdir time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_mknod(const char *path_orig, mode_t mode, dev_t rdev)
{
	char   *fname;
	int     i, fd, res, succ_cnt=0, fail_cnt=0, *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@mknod %s\n", path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
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
			dbg("mknod/open+chown: %s\n", path_orig);

		} else if (S_ISFIFO(mode)) {
			res = mkfifo(fname, mode);
			if (res) {
				err_list[i] = -errno;
				fail_cnt++;
				free(fname);
				continue;
			}
			dbg("mknod/fifo+chown: %s\n",path_orig);

		} else {
			free(fname);
			reacquire_priv();
			return -ENOSYS;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("mknod", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2, NULL);
	timeval_subtract(&t3, &t2 ,&t1);
	dbg("mknod time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_truncate(const char *path_orig, off_t size)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@truncate %s\n", path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = truncate(fname, size);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("truncate", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("truncate time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_chmod(const char *path_orig, mode_t mode)
{
	char   *fname;
	int     i, fail_cnt=0, succ_cnt=0;
	int    *err_list, err, res;
	decl_tmvar(t1, t2, t3);

	dbg("\n@chmod %s %o\n", path_orig, mode);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = chmod(fname, mode);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("chmod", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("chmod time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_chown(const char *path_orig, uid_t uid, gid_t gid)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@chown %s uid=%u gid=%u\n", path_orig, uid, gid);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = lchown(fname, uid, gid);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("chown", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("chown time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_utime(const char *path_orig, struct utimbuf *buf)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@utime %s\n", path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = utime(fname, buf);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("utime", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("utime time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_rmdir(const char *path_orig)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@rmdir %s\n", path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = rmdir(fname);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("rmdir", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("rmdir time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_unlink(const char *path_orig)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@unlink %s\n", path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = unlink(fname);
		if (res < 0) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("unlink", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("unlink time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_mkdir(const char *path_orig, mode_t mode)
{
	char   *fname;
	int     i, res;
	int     fail_cnt=0, succ_cnt=0;
	int    *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@mkdir %s\n",path_orig);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(path_orig, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = mkdir(fname, mode);
		if (res) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("mkdir", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("mkdir time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_symlink(const char *from, const char *to)
{
	char *fname;
	int   i, res, fail_cnt=0, succ_cnt=0, *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@symlink %s->%s\n", from, to);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname = xlate(to, config.replicas[i].path);
		if (!fname) {
			continue;
		}

		res = symlink(from, fname);
		if (res) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname);
			continue;
		}
		succ_cnt++;
		free(fname);
	}

	disable_faulty_replicas("symlink", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("symlink time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}

static int chiron_rename(const char *from, const char *to)
{

	char   *fname_from=NULL, *fname_to=NULL;
	int     i, res, fail_cnt=0, succ_cnt=0, *err_list, err;
	decl_tmvar(t1, t2, t3);

	dbg("\n@rename %s->%s\n", from, to);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname_from = xlate(from, config.replicas[i].path);
		if (!fname_from) {
			continue;
		}

		fname_to = xlate(to,config.replicas[i].path);
		if (!fname_to) {
			free(fname_from);
			continue;
		}

		res = rename(fname_from, fname_to);
		if (res) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname_from);
			free(fname_to);
			continue;
		}
		free(fname_from);
		free(fname_to);
		succ_cnt++;
	}

	disable_faulty_replicas("rename", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("rename time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;

}

static int chiron_link(const char *from, const char *to)
{
	char   *fname_from=NULL, *fname_to=NULL;
	int     i, res, fail_cnt=0, succ_cnt=0, *err_list, err;
	decl_tmvar(t1, t2, t3);

	//do_by2names_rw(0,link(fname_from, fname_to), "link");

	dbg("\n@link %s->%s\n", from, to);
	drop_priv();
	gettmday(&t1,NULL);

	err_list = calloc(config.max_replica, sizeof(int));
	if (!err_list) {
		reacquire_priv();
		return -ENOMEM;
	}

	for(i = 0; i < config.max_replica; i++) {
		if (config.replicas[i].disabled) {
			continue;
		}

		fname_from = xlate(from, config.replicas[i].path);
		if (!fname_from) {
			continue;
		}

		fname_to = xlate(to,config.replicas[i].path);
		if (!fname_to) {
			free(fname_from);
			continue;
		}

		res = link(fname_from, fname_to);
		if (res) {
			err_list[i] = -errno;
			fail_cnt++;
			free(fname_from);
			free(fname_to);
			continue;
		}
		free(fname_from);
		free(fname_to);
		succ_cnt++;
	}

	disable_faulty_replicas("link", succ_cnt, fail_cnt, err_list);

	if (!succ_cnt) {
		err = get_first_error(err_list);
		free(err_list);
		reacquire_priv();
		return err;
	}
	free(err_list);

	gettmday(&t2,NULL);
	timeval_subtract(&t3,&t2,&t1);
	dbg("link time %ld secs, %ld usecs\n", t3.tv_sec, t3.tv_usec);

	reacquire_priv();
	return 0;
}


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
    .destroy      = NULL,
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
    .flush        = NULL,
#ifdef HAVE_SETXATTR
    .setxattr     = NULL,
    .getxattr     = NULL,
    .listxattr    = NULL,
    .removexattr  = NULL,
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

	config.uid = geteuid();
	config.gid = getegid();

	//mount_ctl = 1;
	//config.chironctl_mountpoint = do_realpath(optarg,NULL);

	do_mount(options.replica_args, options.mountpoint);
	res = fuse_main(args.argc, args.argv, &chiron_oper);

	return res;
}

