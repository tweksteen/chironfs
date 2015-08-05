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

ctlfs_entry_t   ctlfs[2];
//ctlfs_entry_t   ctl_state_file;
int             max_replica          = 0;
char           *mount_point          = NULL;
char           *chironctl_mountpoint = NULL;
replica_t      *replicas             = NULL;
unsigned long   inode_count          = 0;
char           *fuse_options         = "-oallow_other,debug";
char           *chironctl_parentdir  = NULL;
FILE           *tochironfs           = NULL;
FILE           *fromchironfs         = NULL;
pthread_mutex_t comm                 = PTHREAD_MUTEX_INITIALIZER;
char            status_fname[]       = { "/status" };
#define         NAGIOS_FNAME           "check_chironfs.sh"
char            nagios_fname[]       = { "/" NAGIOS_FNAME };
char            nagios_script[]      = {
"#!/bin/sh\n"
"\n"
"print_version() {\n"
"   echo 'This is the ChironFS Nagios check plugin version " PACKAGE_VERSION ".'\n"
"}\n"
"\n"
"print_usage() {\n"
"   echo 'Usage: " NAGIOS_FNAME "'\n"
"}\n"
"\n"
"print_help() {\n"
"   print_version\n"
"   echo ''\n"
"   print_usage\n"
"   echo ''\n"
"   echo 'This plugin checks the status of ChironFS replicas.'\n"
"   echo ''\n"
"   exit 0\n"
"}\n"
"\n"
"case \"$1\" in\n"
"   --help)\n"
"      print_help\n"
"      exit 0\n"
"      ;;\n"
"   -h)\n"
"      print_help\n"
"      exit 0\n"
"      ;;\n"
"   --version)\n"
"      print_version\n"
"      exit 0\n"
"      ;;\n"
"   -V)\n"
"      print_version\n"
"      exit 0\n"
"      ;;\n"
"   *)\n"
"      echo                                                     \n"
"      exit  \n"
"      ;;\n"
"esac\n"
};

char status_msgs[3][53] = {
	{ "OK - ChironFS replica enabled                       " },
	{ "WARNING - ChironFS replica enabled, but inconsistent" },
	{ "CRITICAL - ChironFS replica disabled                " }
};

ctlfs_entry_t mkstatnod(char *path, unsigned long mode, unsigned short uid, unsigned short gid)
{
	ctlfs_entry_t c;

	memset(&c,0,sizeof(ctlfs_entry_t));
	c.path                  = path;
	c.attr.st_mode          = mode;
	c.attr.st_ino           = ++inode_count;
	c.attr.st_dev           = 0;
	c.attr.st_nlink         = 0;
	c.attr.st_uid           = uid;
	c.attr.st_gid           = gid;
	c.attr.st_size          = 1;
	c.attr.st_atime         = 0;
	c.attr.st_mtime         = 0;
	c.attr.st_ctime         = 0;
	c.attr.st_blocks        = 0;
	c.attr.st_blksize       = 0;
	c.ctlfs                 = NULL;
	return(c);
}

void free_ctlnode(ctlfs_entry_t *ctlroot)
{
	ctlfs_entry_t *c = ctlroot;
	for(;c->path!=NULL;++c) {
		if (c->ctlfs!=NULL) {
			free_ctlnode(c->ctlfs);
		}
		free(c->path);
	}
	free(ctlroot);
}

int mkctlfs()
{
	int         i;
	struct stat st;
	//   uid_t       uid = geteuid();
	//   gid_t       gid = getegid();


	i = stat(chironctl_mountpoint,&st);
	if (i) {
		return(-1);
	}

	ctlfs[0]       = mkstatnod("/",S_IFDIR | S_IRUSR | S_IXUSR | ((S_IRUSR | S_IXUSR)>>3),st.st_uid,st.st_gid);
	ctlfs[0].ctlfs = malloc(sizeof(ctlfs_entry_t)*max_replica+1);
	if (ctlfs[0].ctlfs==NULL) {
		return(-1);
	}
	ctlfs[1].path = NULL;
	dbg("\nalloced");

	for(i=0;i<max_replica;++i) {
		ctlfs->ctlfs[i] = mkstatnod(malloc(sizeof(char) * (2+strlen(replicas[i].path))),
					    S_IFDIR | S_IRUSR | S_IXUSR | ((S_IRUSR | S_IXUSR)>>3),
					    st.st_uid,st.st_gid);
		if (ctlfs->ctlfs[i].path == NULL) {
			free_ctlnode(ctlfs->ctlfs);
			dbg("\nctl out of mem");
			return(-1);
		}
		sprintf(ctlfs->ctlfs[i].path,"%s",replicas[i].path);
		dbg("\nctl: path=%s, path2=%s ino=%d",replicas[i].path, ctlfs->ctlfs[i].path, ctlfs->ctlfs[i].attr.st_ino);

		// replica subdir
		ctlfs->ctlfs[i].ctlfs = malloc(sizeof(ctlfs_entry_t)*3);
		if (ctlfs->ctlfs[i].ctlfs==NULL) {
			free_ctlnode(ctlfs->ctlfs);
			return(-1);
		}
		// end of the list
		ctlfs->ctlfs[i].ctlfs[2].path = NULL;
		// status file
		ctlfs->ctlfs[i].ctlfs[0] = mkstatnod(malloc(sizeof(char) * (2+strlen(status_fname))),
						     S_IFREG | S_IRUSR | S_IWUSR | ((S_IRUSR | S_IWUSR)>>3),
						     st.st_uid,st.st_gid);
		if (ctlfs->ctlfs[i].ctlfs[0].path == NULL) {
			free_ctlnode(ctlfs->ctlfs);
			dbg("\nctl out of mem");
			return(-1);
		}
		sprintf(ctlfs->ctlfs[i].ctlfs[0].path,status_fname);
		dbg("\nctl: status ino=%d",ctlfs->ctlfs[i].ctlfs[0].attr.st_ino);
		// nagios plugin script
		ctlfs->ctlfs[i].ctlfs[1] = mkstatnod(malloc(sizeof(char) * (2+strlen(nagios_fname))),
						     S_IFREG | S_IRUSR | S_IXUSR | ((S_IRUSR | S_IXUSR)>>3),
						     st.st_uid,st.st_gid);
		if (ctlfs->ctlfs[i].ctlfs[1].path == NULL) {
			free_ctlnode(ctlfs->ctlfs);
			dbg("\nctl out of mem");
			return(-1);
		}
		ctlfs->ctlfs[i].ctlfs[1].attr.st_size = strlen(nagios_script);
		sprintf(ctlfs->ctlfs[i].ctlfs[1].path,nagios_fname);
		dbg("\nctl: status ino=%d",ctlfs->ctlfs[i].ctlfs[1].attr.st_ino);
	}
	ctlfs->ctlfs[i].path = NULL;

	return(0);
}

ctlfs_search_t find_path(const char *path, ctlfs_entry_t *c, int deep)
{
	ctlfs_search_t res;
	size_t         s;
	int            i;

	for(i=0;c[i].path!=NULL;++i) {
		if (!strcmp(path,c[i].path)) {
			res.ctlfs = c;
			res.i     = i;
			return(res);
		}
		s = strlen(c[i].path);
		if ((!strncmp(path,c[i].path,s)) && (path[s]=='/') && (c[i].ctlfs!=NULL)) {
			if (!deep) {
				res.ctlfs = c;
				res.i     = i;
				return(res);
			}
			return(find_path(path+s, c[i].ctlfs,deep-1));
		}
	}
	res.ctlfs = NULL;
	res.i     = 0;
	return(res);
}

char *get_daddy(const char *path)
{
	char *s, *p;

	if (path[1]=='\0') {
		s = strdup("");
		return(s);
	}
	s = strdup(path);
	if (s==NULL) {
		return(s);
	}
	p = strrchr(s,'/');
	if (p==s) {
		s[1] = '\0';
	} else {
		(*p) = '\0';
	}
	return(s);
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

int get_perm(uid_t uid, gid_t gid, struct stat st)
{
	int   i, res=0;
	char *buffer;
	struct group result_buf, *result=NULL;
	struct passwd *pw;
	char         **member;
	dbg("\ncuid:%d\tstuid:%d",uid,st.st_uid);
	if (uid==st.st_uid) {
		return((st.st_mode&0700) >> 6);
	}
	if (gid==st.st_gid) {
		return((st.st_mode&070) >> 3);
	}
	i = 1024;
	do {
		buffer     = calloc(i,sizeof(char));
		if (buffer!=NULL) {
			res = getgrgid_r(st.st_gid,&result_buf,buffer,i,&result);
			if (res==ERANGE) {
				free(buffer);
				i <<= 1;
			}
		}
	} while ((res==ERANGE) && (buffer!=NULL));
	if (buffer==NULL) {
		errno = ENOMEM;
		return(-1);
	}
	if (result!=NULL) {
		member = result->gr_mem;
		while (*member) {
			pw = getpwnam(*member);
			if (pw->pw_uid==uid) {
				free(buffer);
				return((st.st_mode&070) >> 3);
			}
			member++;
		}
	}
	free(buffer);
	return(st.st_mode&7);
}

int get_path_perm(const char *path)
{
	ctlfs_search_t res, path_res;

	static struct fuse_context *context;
	int           perm, path_perm;

	context  = fuse_get_context();

	if ((!context->uid) || (!context->gid)) {
		return(7);
	}

	perm = get_perm(context->uid,context->gid,ctlfs[0].attr);

	if (path[1]=='\0') {
		dbg("\nperm (/):%d",perm);
		return(perm);
	}

	if (perm&1) {
		path_perm = perm;

		res = find_path(path,ctlfs->ctlfs,0);
		if (res.ctlfs==NULL) {
			return(-ENOENT);
		}
		perm = get_perm(context->uid,context->gid,res.ctlfs->attr);
		path_res = res;

		res = find_path(path,ctlfs->ctlfs,1);
		if ((res.ctlfs==path_res.ctlfs) && (res.i==path_res.i)) {
			dbg("\nperm:%d",perm);
			return(perm);
		}
		if (perm&1) {
			perm = get_perm(context->uid,context->gid,res.ctlfs->attr);
			dbg("\nperm:%d",perm);
			return(perm);
		}
	}
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


static int chironctl_statfs(const char *path, struct statvfs *stbuf)
{
	(void) path;
	stbuf->f_bsize  = 4096;
	stbuf->f_frsize = 4096;
	stbuf->f_bsize  = 4096;
	stbuf->f_frsize = 4096;
	stbuf->f_blocks = 0xFFFFF;
	stbuf->f_bfree  = 0xFFFFF;
	stbuf->f_bavail = 0xFFFFF;
	stbuf->f_files  = 0xFFFF;
	stbuf->f_ffree  = 0xFFFF;
	stbuf->f_favail = 0xFFFF;
	stbuf->f_fsid   = -1047;
	stbuf->f_flag   = 1024;
	stbuf->f_namemax= 255;
	return(0);
}


static int chironctl_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			     off_t offset, struct fuse_file_info *fi)
{
	ctlfs_entry_t *dir;
	ctlfs_search_t res;

	(void) offset;
	(void) fi;
	dbg("\nctlreaddir: %s, offset: %ld\n",path,offset);
	if ((get_path_perm(path)&5)!=5) {
		return(-EACCES);
	}

	if (path[1]=='\0') {
		dir = ctlfs;
	} else {
		res = find_path(path,ctlfs->ctlfs,-1);
		if (res.ctlfs==NULL) {
			return(-ENOENT);
		}
		dir = res.ctlfs+res.i;
	}
	filler(buf, ".", &(dir->attr), 0);
	//   lstat(chironctl_parentdir,&st);
	//   filler(buf, "..", &st, 0);
	for(dir = dir->ctlfs;dir->path!=NULL;++dir) {
		if (filler(buf, dir->path+1, &(dir->attr), 0)) {
			//                                                        dbg("\nctlreaddir: i=%d\n",i);
			return 0;
		}
	}
	return 0;
}

static void chironctl_destroy(void *notused)
{
	(void) notused;
}

static int chironctl_truncate(const char *path_orig, off_t size)
{
	dbg("\ntruncate: %s\n",path_orig);
	(void) path_orig;
	(void) size;
	return(0);
}

static int chironctl_getattr(const char *path, struct stat *stbuf)
{
	dbg("\nctlgetattr: %s\n",path);
	ctlfs_entry_t  *dir;
	ctlfs_search_t  res;
	char           *p;
	int             perm;

	p = get_daddy(path);
	if (p==NULL) {
		return(-ENOMEM);
	}
	dbg("\nctlgetattr-daddy: %s\n",p);

	if (p[0]) {
		perm = get_path_perm(p);
		dbg("\nctlgetattr-perm: %d\n",perm);
		if ((perm&5)!=5) {
			return(-EACCES);
		}
	} else {
		if ((get_path_perm(path)&5)!=5) {
			return(-EACCES);
		}
	}

	dbg("\nctlgetattr-perm: ok, path=%s\n",path);

	if (path[1]=='\0') {
		dir = ctlfs;
	} else {
		res = find_path(path,ctlfs->ctlfs,-1);
		if (res.ctlfs==NULL) {
			dbg("\nctlgetattr-404: notfound\n");
			return(-ENOENT);
		}
		dir = res.ctlfs+res.i;
	}
	dbg("\nctlgetattr: mode=%o\n",dir->attr.st_mode);

	memcpy(stbuf, &(dir->attr), sizeof(struct stat));
	return(0);
}

static int chironctl_open(const char *path, struct fuse_file_info *fi)
{
	int perm = 0, perm_mask = 0;
	ctlfs_search_t res;
	(void) fi;

	if (fi->flags&O_CREAT) {
		return(-EACCES);
	}

	if (fi->flags&(O_WRONLY|O_TRUNC|O_APPEND)) {
		perm_mask = 2;
	} else if (fi->flags&O_RDWR) {
		perm_mask = 6;
	} else if ((fi->flags&(O_RDONLY|O_EXCL)) == (O_RDONLY)) {
		perm_mask = 4;
	}
	perm = get_path_perm(path);
	if (((perm&perm_mask)!=perm_mask) || (!perm_mask) || (!perm)) {
		return(-EACCES);
	}

	if (path[1]!='\0') {
		res = find_path(path,ctlfs->ctlfs,-1);
		if (res.ctlfs==NULL) {
			return(-ENOENT);
		}
	}
	return 0;
}

static int chironctl_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	int sz, rep, someerr, fname;
	ctlfs_search_t res;
	ctlfs_entry_t *cmd;
	char          *chbuf = NULL;

	(void) fi;

	if ((get_path_perm(path)&4)!=4) {
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
	dbg("\nctlread: rep: %d, cmd: %s\n",rep,cmd->path);
	fname = (!strcmp(cmd->path,status_fname))
		? 1
		: (
		   (!strcmp(cmd->path,nagios_fname))
		   ? 2
		   : 0
		  );
	if (fname) {
		pthread_mutex_lock(&comm);

		fprintf(tochironfs,"0007stat:%02X",res.i);
		fflush(tochironfs);
		someerr = read_a_line(&chbuf,&sz,fromchironfs);
		pthread_mutex_unlock(&comm);
		if (someerr) {
			return (-EIO);
		}
		dbg("\nctlread: rep: %d, cmd: %s=%s\n",rep,cmd->path,chbuf);

		if (fname==1) {
			sprintf(buf,"%c",chbuf[0]);
			free(chbuf);
			return(1);
		}
		if (fname==2) {
			sz = strlen(nagios_script) - offset;
			if (sz<0) {
				sz = 0;
			}
			sz = min(sz,(int)size);

			memcpy(nagios_script+538,status_msgs[chbuf[0]-'0'],strlen(status_msgs[chbuf[0]-'0']));
			nagios_script[602] = chbuf[0];
			memcpy(buf,nagios_script+offset,sz);
			free(chbuf);
			return(sz);
		}
	}

	return(-ENOSYS);
}

static int chironctl_write(const char *path, const char *buf, size_t size,
			   off_t offset, struct fuse_file_info *fi)
{
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

	return(-ENOSYS);
}

struct fuse_operations chironctl_oper = {
	.readdir      = chironctl_readdir,
	.statfs       = chironctl_statfs,
	.destroy      = chironctl_destroy,
	.getattr      = chironctl_getattr,
	.open         = chironctl_open,
	.read         = chironctl_read,
	.write        = chironctl_write,
	.truncate     = chironctl_truncate,
};


void free_vars(void)
{
	int i;
	if (chironctl_parentdir!=NULL) {
		free(chironctl_parentdir);
	}
	if (chironctl_mountpoint!=NULL) {
		free(chironctl_mountpoint);
	}
	if (mount_point!=NULL) {
		free(mount_point);
	}
	if (replicas!=NULL) {
		for(i=0;i<max_replica;++i) {
			if (replicas[i].path != NULL) {
				free(replicas[i].path);
			}
		}
		free(replicas);
	}
}

int main(int argc, char *argv[])
{
	int res, sz, someerr, i, j, l;
	char *buf= NULL, *ptr;
	char *fuse_argv[4];

	(void) argc;
	dbg("chironctl: started");
	printf("0004info");
	fflush(stdout);
	someerr = read_a_line(&buf,&sz,stdin);
	if (someerr) {
		exit(-1);
	}
	dbg("chironfs mnt: %s",buf);
	mount_point = strdup(buf);
	if (mount_point==NULL) {
		dbg("no mem");
		free(buf);
		exit(-1);
	}
	someerr = read_a_line(&buf,&sz,stdin);
	if (someerr) {
		free_vars();
		exit(-1);
	}
	dbg("chironctl mnt: %s",buf);
	chironctl_mountpoint = strdup(buf);
	if (chironctl_mountpoint==NULL) {
		dbg("no mem");
		free(buf);
		free_vars();
		exit(-1);
	}

	ptr = strrchr(chironctl_mountpoint,'/');
	if (ptr==chironctl_mountpoint) {
		chironctl_parentdir = strdup("/");
	} else {
		chironctl_parentdir = malloc(sizeof(char) * ((ptr-chironctl_mountpoint)+1));
		if (chironctl_parentdir!=NULL) {
			strncpy(chironctl_parentdir,chironctl_mountpoint,ptr-chironctl_mountpoint);
		}
	}
	if (chironctl_parentdir==NULL) {
		free(buf);
		free_vars();
		exit(-1);
	}

	someerr = read_a_line(&buf,&sz,stdin);
	if (someerr) {
		free_vars();
		exit(-1);
	}
	sscanf(buf,"%X",&max_replica);
	dbg("max-replica: %s (%d)",buf,max_replica);
	replicas = malloc(sizeof(replica_t) * max_replica);
	if (replicas==NULL) {
		dbg("no mem");
		free(buf);
		free_vars();
		exit(-1);
	}
	for(i=0;i<max_replica;++i) {
		replicas[i].path = NULL;
	}
	for(i=0;i<max_replica;++i) {
		someerr = read_a_line(&buf,&sz,stdin);
		if (someerr) {
			free_vars();
			exit(-1);
		}
		dbg("replica: %s",buf);
		replicas[i].path = malloc(sizeof(char) * strlen(buf) + 2);
		if (replicas[i].path==NULL) {
			dbg("no mem");
			free(buf);
			free_vars();
			exit(-1);
		}
		replicas[i].path[0]='/';
		for(l=strlen(buf),j=0;j<l;++j) {
			if (buf[j]=='/') {
				replicas[i].path[j+1] = '_';
			} else {
				replicas[i].path[j+1] = buf[j];
			}
		}
	}

	if ((res = mkctlfs())) {
		dbg("%s",buf);
		free(buf);
		free_vars();
		exit(-res);
	}

	fromchironfs = fdopen(STDIN_FILENO,"r");
	tochironfs   = fdopen(STDOUT_FILENO,"a");
	close(STDERR_FILENO);

	fuse_argv[0] = argv[0];
	fuse_argv[1] = chironctl_mountpoint;
	fuse_argv[2] = fuse_options;
	fuse_argv[3] = NULL;
	dbg("starting fuse");
	res = fuse_main(3, fuse_argv, &chironctl_oper);
	dbg("ending fuse: %d", res);
	free(buf);
	free_vars();
	return(res);
}

