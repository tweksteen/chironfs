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

#include "ctl.h"

struct chironctl_config config = {
	.root = NULL,
	.max_replica = 0,
	.mountpoint = NULL,
	.chironctl_mountpoint = NULL,
	.replicas = NULL,
	.inode_count = 1,
	.to_chironfs = NULL,
	.from_chironfs = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

char status_msgs[3][53] = {
	{ "OK - ChironFS replica enabled                       " },
	{ "WARNING - ChironFS replica enabled, but inconsistent" },
	{ "CRITICAL - ChironFS replica disabled                " }
};

ctlfs_entry_t *mkstatnod(char *path, unsigned long mode, uid_t  uid,
			gid_t gid)
{
	ctlfs_entry_t *c;

	c = malloc(sizeof(ctlfs_entry_t));
	c->path         = path;
	c->attr.st_mode = mode;
	c->attr.st_ino  = config.inode_count++;
	c->attr.st_uid  = uid;
	c->attr.st_gid  = gid;
	c->attr.st_size = 1;
	c->children     = NULL;
	c->n_children   = 0;
	return c;
}

/* 
 * Build the virtual file system. One subdirectory per replica, named after the
 * replica's number. Each replica's directory contains a status file.
 */
int mkctlfs()
{
	int i;
	struct stat st;
	char *replica_path;
	ctlfs_entry_t *replica, *status;

	i = stat(config.chironctl_mountpoint, &st);
	if (i) {
		return -1;
	}
	config.root = mkstatnod("/", S_IFDIR | S_IRUSR | S_IXUSR |
				S_IRGRP | S_IXGRP, st.st_uid,st.st_gid);
	config.root->n_children = config.max_replica;
	config.root->children = malloc(config.root->n_children *
				      sizeof(ctlfs_entry_t *));
	if (!config.root->children) {
		return -1;
	}

	/* Create a sub directory for each replica */
	for(i = 0; i < config.max_replica; i++) {
		if (asprintf(&replica_path, "%u", i) < 0) {
			return -1;
		}
		replica = mkstatnod(replica_path, S_IFDIR | S_IRUSR | S_IXUSR |
				    S_IRGRP | S_IXGRP, st.st_uid, st.st_gid);
		config.root->children[i] = replica;
		replica->n_children = 1;
		replica->children = malloc(replica->n_children *
					   sizeof(ctlfs_entry_t *));
		if (!replica->children) {
			return -1;
		}
		status = mkstatnod(STATUS_FNAME, S_IFREG | S_IRUSR | S_IWUSR |
				   S_IRGRP | S_IWGRP, st.st_uid,st.st_gid);
		replica->children[0] = status;
	}
	return 0;
}

ctlfs_entry_t *find_entry_by_path(const char *path)
{
	unsigned int i, found;
	char *p, *s, *tok;
	ctlfs_entry_t *current, *child;

	p = strdup(path);
	if(!p) {
		return NULL;
	}

	for(current = config.root, s = p; ; s = NULL) {
		tok = strtok(s, "/");
		if(!tok)
			break;
		dbg("token is %s\n", tok);
		found = 0;
		for(i = 0; i < current->n_children; i++) {
			dbg("testing children[%u] path=%s\n", i,
			    current->children[i]->path);
			child = current->children[i];
			if (!strcmp(child->path, tok)) {
				current = child;
				found = 1;
				dbg("found it %s\n", child->path);
				break;
			}
		}
		if(!found)
			return NULL;
	}
	return current;

}


static int chironctl_readdir(const char *path, void *buf,
			     fuse_fill_dir_t filler, off_t offset,
			     struct fuse_file_info *fi)
{
	unsigned int i;
	ctlfs_entry_t *d, *c;

	dbg("[ctl] @readdir: %s, offset: %ld\n", path, offset);

	d = find_entry_by_path(path);
	if(!d) {
		return -ENOENT;
	}

	filler(buf, ".", &(d->attr), 0);
	for(i = 0; i < d->n_children; i++) {
		c = d->children[i];
		if (filler(buf, c->path, &(c->attr), 0)) {
			break;
		}
	}
	return 0;
}

static int chironctl_getattr(const char *path, struct stat *stbuf)
{
	ctlfs_entry_t  *d;
	dbg("[ctl] @getattr: %s\n", path);

	d = find_entry_by_path(path);
	if(!d) {
		return -ENOENT;
	}

	memcpy(stbuf, &(d->attr), sizeof(struct stat));
	return 0;
}

static int chironctl_open(const char *path, struct fuse_file_info *fi)
{
	ctlfs_entry_t  *d;
	dbg("[ctl] @open: %s\n", path);

	d = find_entry_by_path(path);
	if(!d) {
		return -ENOENT;
	}

	return 0;
}

static int chironctl_read(const char *path, char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	int res, err, sz;
	unsigned int replica;
	char *p, *replica_s, *chbuf;
	ctlfs_entry_t *d;

	dbg("[ctl] @read %s\n", path);

	d = find_entry_by_path(path);
	if (!d || strcmp(d->path, "status")) {
		return -ENOENT;
	}

	dbg("d->path == status\n");

	p = strdup(path);
	if(!p) {
		return -ENOMEM;
	}
	dbg("1\n");
	p = dirname(p);
	if(!p) {
		return -ENOENT;
	}
	dbg("2\n");
	replica_s = basename(p);
	if(!replica_s) {
		return -ENOENT;
	}
	dbg("3\n");
	res = sscanf(replica_s, "%u", &replica);
	dbg("res = %d, replica_s = %s, replica = %d\n", res, replica_s, replica);
	if(!res || replica >= config.max_replica) {
		return -ENOENT;
	}
	dbg("4\n");
	free(p);

	dbg("[ctl] @read rep= %d\n", replica);

	pthread_mutex_lock(&config.mutex);
	fprintf(config.to_chironfs,"0007stat:%02X", replica);
	fflush(config.to_chironfs);
	err = read_a_line(&chbuf, &sz, config.from_chironfs);
	pthread_mutex_unlock(&config.mutex);
	if (err) {
		return -EIO;
	}
	dbg("[ctl] replica=%u, chbuf=%s\n", replica, chbuf);

	sprintf(buf,"%c",chbuf[0]);
	free(chbuf);
	return 1;
}

static int chironctl_write(const char *path, const char *buf, size_t size,
			   off_t offset, struct fuse_file_info *fi)
{
	/*
	int sz, rep, someerr;
	ctlfs_search_t res;
	ctlfs_entry_t *cmd;
	char          *chbuf = NULL;

	(void) fi;
	(void) offset;
	dbg("\nctlwrite: %d=%s\n",size,buf);
	if ((get_path_perm(path)&2)!=2) {
		return(-EACCES);
	}

	if (path[1]=='\0') {
		return(-ENOSYS);
	}
	res = find_path(path,ctlfs->ctlfs,-1);
	if (res.ctlfs==NULL) {
		return(-ENOENT);
	}
	cmd = res.ctlfs+res.i;

	res = find_path(path,ctlfs->ctlfs,0);
	rep = res.i;

	if (!strcmp(cmd->path,status_fname)) {
		if ((size>2) 
		    || ((size==2) && (buf[1]!='\n'))
		    //      || ((buf[0]!='0') && (buf[0]!='1') && (buf[0]!='2'))
		    || ((buf[0]!='0') && (buf[0]!='2'))
		   ) {
			return(-ENOSYS);
		}
		pthread_mutex_lock(&comm);

		if (buf[0]=='0') {
			fprintf(tochironfs,"0008trust:%02X",res.i);
		} else if (buf[0]=='1') {
			fprintf(tochironfs,"0009enable:%02X",res.i);
		} else if (buf[0]=='2') {
			fprintf(tochironfs,"000Adisable:%02X",res.i);
		}
		fflush(tochironfs);
		someerr = read_a_line(&chbuf,&sz,fromchironfs);
		pthread_mutex_unlock(&comm);
		if (someerr) {
			return (-EIO);
		}
		dbg("\nctlwrite: rep: %d, cmd: %s=%s\n",rep,cmd->path,chbuf);

		if ((chbuf[0]=='O') && (chbuf[1]=='K')) {
			free(chbuf);
			return(size);
		}
		free(chbuf);
		return(-EIO);
	}
	*/
	return(-ENOSYS);
}

struct fuse_operations chironctl_oper = {
	.readdir      = chironctl_readdir,
	.getattr      = chironctl_getattr,
	.open         = chironctl_open,
	.read         = chironctl_read,
	.write        = chironctl_write,
};

int main(int argc, char *argv[])
{
	int res, sz, err, i;
	char *buf = NULL;
	char *fuse_argv[5];

	dbg("[ctl] chironctl started\n");
	printf("0004info");
	dbg("about to flush\n");
	fflush(stdout);
	dbg("reading a line\n");
	err = read_a_line(&buf, &sz, stdin);
	if (err) {
		return -1;
	}
	dbg("[ctl] buf = %s\n", buf);
	config.mountpoint = strdup(buf);
	if (!config.mountpoint) {
		return -1;
	}
	dbg("[ctl] config.mountpoint = %s\n", config.mountpoint);
	err = read_a_line(&buf,&sz,stdin);
	if (err) {
		return -1;
	}
	config.chironctl_mountpoint = strdup(buf);
	if (!config.chironctl_mountpoint) {
		return -1;
	}
	dbg("[ctl] config.chironctl_mountpoint = %s\n",
	    config.chironctl_mountpoint);

	err = read_a_line(&buf,&sz,stdin);
	if (err) {
		return -1;
	}
	sscanf(buf, "%X", &config.max_replica);
	dbg("[ctl] config.max_replica = %d\n", config.max_replica);

	config.replicas = calloc(config.max_replica, sizeof(replica_t));
	if (!config.replicas) {
		return -1;
	}

	for(i = 0; i < config.max_replica; i++) {
		err = read_a_line(&buf, &sz, stdin);
		if (err) {
			return -1;
		}
		config.replicas[i].path = strdup(buf);
		if (!config.replicas[i].path) {
			return -1;
		}
		config.replicas[i].pathlen = strlen(config.replicas[i].path);
	}

	dbg("[ctl] config.replicas = [ \n");
	for(i = 0; i < config.max_replica; i++) {
		dbg("\t[%d] path=%s pathlen=%u\n", i,
		    config.replicas[i].path, config.replicas[i].pathlen);
	}
	dbg("]\n");

	if ((res = mkctlfs())) {
		return -1;
	}

	config.from_chironfs = fdopen(STDIN_FILENO,"r");
	config.to_chironfs   = fdopen(STDOUT_FILENO,"a");
	close(STDERR_FILENO);

	fuse_argv[0] = argv[0];
	fuse_argv[1] = config.chironctl_mountpoint;
	fuse_argv[2] = "-odefault_permissions,allow_other";
	fuse_argv[3] = "-f";
	fuse_argv[4] = NULL;
	res = fuse_main(4, fuse_argv, &chironctl_oper, NULL);
	return res;
}
