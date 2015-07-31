/* Copyright 2005-2008, Luis Furquim
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as 
 * published by the Free Software Foundation.
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

#include "conf.h"
#define _GNU_SOURCE /* for access to canonicalize_file_name() inside stdlib.h
		       and the constant O_NOFOLLOW inside fcntl.h */

void print_paths()
{
	int i, j;
	dbg("\n(%s):",mount_point);
	for(i=0;i<max_replica;++i) {
		dbg("\n   (%s)",paths[i].path);
	}
	dbg("\nHIGH\n");
	for(i=0;i<max_replica_high;++i) {
		dbg("\n(");
		for(j=0;j<max_replica_high;++j) {
			dbg("%d ",round_robin_high[i][j]);
		}
		dbg(")");
	}
	dbg("\nLOW\n");
	for(i=0;i<max_replica_low;++i) {
		dbg("\n(");
		for(j=0;j<max_replica_low;++j) {
			dbg("%d ",round_robin_low[i][j]);
		}
		dbg(")");
	}
	dbg("\n");
}

void free_paths()
{
	int i;
	if (mount_point!=NULL) {
		free(mount_point);
	}
	if (paths[0].path!=NULL) {
		if (strcmp(paths[0].path,".")) {
			free(paths[0].path);
			paths[0].path = NULL;
		}
	}
	for(i=1;i<max_replica;++i) {
		if (paths[i].path!=NULL) {
			free(paths[i].path);
			paths[i].path = NULL;
		}
	}
	free(paths);
	if (logfd!=NULL) {
		fclose(logfd);
		logfd = NULL;
	}
}

void free_tab_fd()
{
	//   int i;
	//      while (i<max_replica) {
	//         free(tab_fd.fd[i++]);
	//      }
	free(tab_fd.fd);
}

void free_round_robin(int **rr, int max_rep)
{
	int i;
	if (rr!=NULL) {
		for(i=0;i<max_rep;++i) {
			if (rr[i]!=NULL) {
				free(rr[i]);
				rr[i] = NULL;
			}
		}
		free(rr);
	}
}


void opt_parse(char *fo, char**logname, char**argvbuf)
{
	int    opt, start, dest;
	size_t i, len;

	start = opt = i = 0;
	len   = strlen(fo);
	do {
		if ((!fo[i]) || (fo[i]==',')) {
			if (!strncmp(fo+start,"log",3)) {
				fo[i] = 0;
				(*logname) = chiron_realpath(fo+start+4);
				if ((*logname)==NULL) {
					print_err(errno,fo+start+4);
					exit(errno);
				}
				dbg("\nlog=%s", fo+start);
				if (start) {
					dest = start-1;
				} else {
					dest = start;
				}
				sprintf(fo+dest,"%s",fo+start+4+strlen(*logname));
				len -= i - start + 1;
				i = start - 1;
			} else if (!strncmp(fo+start,"fsname",6)) {
				fo[i] = 0;
				(*argvbuf) = strdup(fo+start);
				if ((*argvbuf)==NULL) {
					print_err(CHIRONFS_ERR_LOW_MEMORY,"fsname allocation");
					exit(CHIRONFS_ERR_LOW_MEMORY);
				}
				dbg("\nfsname=%s", fo+start);
				if (start) {
					dest = start-1;
				} else {
					dest = start;
				}
				sprintf(fo+dest,"%s",fo+start+strlen(*argvbuf));
				len -= i - start + 1;
				i = start - 1;
			} else if (!strncmp(fo+start,"quiet",5)) {
				fo[i] = 0;
				quiet_mode = 1;
				sprintf(fo+start,"%s",fo+6);
				i = start - 1;
			} else {
				start = i + 1;
			}
		}
		i++;
		dbg("\n%s",fo);
	} while (i<=len);
}

int **mk_round_robin(int *tmp_list,int dim)
{
	int **round_robin, j, i;

	round_robin = calloc(dim,sizeof(int *));
	if (round_robin!=NULL) {
		for(i=0;i<dim;++i) {
			round_robin[i] = calloc(dim,sizeof(int));
			if (round_robin[i]==NULL) {
				while (i) {
					free(round_robin[--i]);
				}
				free(round_robin);
				return(NULL);
			}
			for(j=0;j<dim;++j) {
				if ((j+i)<dim) {
					round_robin[i][j] = tmp_list[j+i];
				} else {
					round_robin[i][j] = tmp_list[j+i-dim];
				}
			}
		}
	}
	return(round_robin);
}


#ifndef __linux__

/*
 * This function was originally written by Antti Kantee
 * Changed by Luis Otavio de Colla Furquim to test the return
 * result from malloc and, in case of failure, return NULL
 * without calling realpath.
 * Yen-Ming Lee has sent another patch, porting ChironFS to
 * FreeBSD also solving this problem
 */
char *do_realpath(const char *path, char *resolvedpath)
{

	if (resolvedpath == NULL) {
		if (!(resolvedpath = malloc(PATH_MAX))) {
			return(NULL);
		}
	}
	return realpath(path, resolvedpath);
}

#endif 



int do_mount(char *filesystems, char *mountpoint)
{
	int    i, start, res, errno, rep_on_mount=0, err;
	int    *tmp_high, *tmp_low;
	unsigned long tmpfd;
	struct rlimit rlp;
	int    oldval;
	size_t oldlenp = sizeof(oldval);
	int    sysctl_names[] = {
#ifdef __linux__
		CTL_FS, FS_MAXFILE
#else
			CTL_KERN, KERN_MAXFILES
#endif
	};




	for(max_replica=1,i=0;filesystems[i];++i) {
		if (filesystems[i]=='=') {
			++max_replica;
			filesystems[i] = 0;
		}
	}

	if (mountpoint[0]==':') {
		max_replica++;
	}

	res = sysctl (sysctl_names, 2, &oldval, &oldlenp, NULL, 0);
	if (res) {
		print_err(errno,"reading system parameter 'max open files'");
		FD_BUF_SIZE = 4096;
	} else {
		FD_BUF_SIZE = (long long unsigned int) oldval;
	}

	tmpfd = (FD_BUF_SIZE >>= 1);

	if (getrlimit(CHIRON_LIMIT,&rlp)) {
		print_err(errno,"reading nofile resource limit");
		exit(errno);
	}
	dbg("\n1cur:%d\tmax:%d",rlp.rlim_cur,rlp.rlim_max);
	rlp.rlim_max = tmpfd;
	dbg("\n2cur:%d\tmax:%d",rlp.rlim_cur,rlp.rlim_max);
	if (rlp.rlim_cur<rlp.rlim_max) {
		rlp.rlim_cur = rlp.rlim_max;
		dbg("\n3cur:%d\tmax:%d",rlp.rlim_cur,rlp.rlim_max);
		if (setrlimit(RLIMIT_NOFILE,&rlp)) {
			dbg("\n4cur:%d\tmax:%d",rlp.rlim_cur,rlp.rlim_max);
			if (getrlimit(RLIMIT_NOFILE,&rlp)) {
				print_err(errno,"reading nofile resource limit, second attempt");
				exit(errno);
			}
		}
	}
	dbg("\n5cur:%d\tmax:%d",rlp.rlim_cur,rlp.rlim_max);

	dbg("\n1tmpfd:%d",tmpfd);
	tmpfd = (rlp.rlim_cur<tmpfd)
		? rlp.rlim_cur
		: tmpfd;
	dbg("\n2tmpfd:%d",tmpfd);

	tmpfd = (tmpfd - 6) / max_replica;

	dbg("\n3tmpfd:%d",tmpfd);
	while (tmpfd) {
		qt_hash_bits++;
		hash_mask = hash_mask<<1 | 1;
		tmpfd >>= 1;
	}
	hash_mask >>= 1;
	FD_BUF_SIZE = hash_mask;
	dbg("\nhash_mask:%x",hash_mask);

	tab_fd.fd = calloc(FD_BUF_SIZE,sizeof(int *));
	if (tab_fd.fd==NULL) {
		print_err(CHIRONFS_ERR_LOW_MEMORY,"file descriptor hash table allocation");
		exit(CHIRONFS_ERR_LOW_MEMORY);
	}

	for(i=0;((unsigned)i)<FD_BUF_SIZE;++i) {
		tab_fd.fd[i] = NULL;
	}

	paths = calloc(max_replica,sizeof(path_t));
	if (paths==NULL) {
		free_paths();
		free_tab_fd();
		print_err(errno,"replica info allocation");
		exit(errno);
	}
	for(i=0;i<max_replica;++i) {
		paths[i].path     = NULL;
		paths[i].disabled = (time_t)0;
		paths[i].priority = 0;
	}

	tmp_high = calloc(max_replica,sizeof(int));
	if (tmp_high==NULL) {
		free_paths();
		free_tab_fd();
		print_err(errno,"high priority round robin table allocation");
		exit(errno);
	}

	tmp_low = calloc(max_replica,sizeof(int));
	if (tmp_low==NULL) {
		free_paths();
		free(tmp_high);
		free_tab_fd();
		print_err(errno,"low priority round robin table allocation");
		exit(errno);
	}

	if (mountpoint[0]==':') {
		mount_point = do_realpath(mountpoint+1,NULL);
		rep_on_mount = i = 1;
		tmp_high[max_replica_high++] = 0;
	} else {
		mount_point = do_realpath(mountpoint,NULL);
		i = 0;
	}
	if (mount_point==NULL) {
		err = errno;
		free_tab_fd();
		print_err(err,mountpoint);
		exit(err);
	}

	start = 0;
	for(;i<max_replica;++i) {
		if (filesystems[start]==':') {
			start++;
			paths[i].priority = 1;
			tmp_low[max_replica_low++] = i;
		} else {
			tmp_high[max_replica_high++] = i;
		}

		paths[i].path = do_realpath(filesystems+start, NULL);
		if (paths[i].path==NULL) {
			free_paths();
			free_tab_fd();
			free(tmp_high);
			free(tmp_low);
			print_err(errno,filesystems+start);
			exit(errno);
		}
		// just to store it and avoid future recalculations
		paths[i].pathlen = strlen(paths[i].path);
		start += strlen(filesystems+start) + 1;
		if (paths[i].priority) {
			call_log("replica priority low",paths[i].path,0);
			dbg("\nreplica low: %s",paths[i].path);
		} else {
			call_log("replica priority high",paths[i].path,0);
			dbg("\nreplica high: %s",paths[i].path);
		}
	}

	if (rep_on_mount) {
		paths[0].path = currdir;
		chdir(mount_point);
	}

	if (logname!=NULL) {
		attach_log();
	}

	round_robin_high = mk_round_robin(tmp_high,max_replica_high);
	if (round_robin_high==NULL) {
		free_paths();
		free_tab_fd();
		free(tmp_high);
		free(tmp_low);
		print_err(CHIRONFS_ERR_LOW_MEMORY,"high priority round robin state table allocation");
		exit(CHIRONFS_ERR_LOW_MEMORY);
	}
	free(tmp_high);

	round_robin_low = mk_round_robin(tmp_low,max_replica_low);
	if (round_robin_low==NULL) {
		free_paths();
		free(tmp_low);
		free_round_robin(round_robin_high,max_replica_high);
		free_tab_fd();
		print_err(CHIRONFS_ERR_LOW_MEMORY,"low priority round robin state table allocation");
		exit(CHIRONFS_ERR_LOW_MEMORY);
	}
	free(tmp_low);

	print_paths();

	return(0);
}


