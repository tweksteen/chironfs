/* Copyright 2005-2008, Luis Furquim
 * Copyright 2015 Thiébaud Weksteen
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

void dump_config()
{
	unsigned int i;
	dbg("config.max_replica = %u\n", config.max_replica);
	dbg("config.max_replica_high = %u\n", config.max_replica_high);
	dbg("config.max_replica_low = %u\n", config.max_replica_low);

	dbg("config.round_robin_high = [ ");
	for(i = 0; i < config.max_replica_high; i++)
		dbg("%d ", config.round_robin_high[i]);
	dbg("]\n");

	dbg("config.round_robin_low = [ ");
	for(i = 0; i < config.max_replica_low; i++)
		dbg("%d ", config.round_robin_low[i]);
	dbg("]\n");

	dbg("config.replicas = [ \n");
	for(i = 0; i < config.max_replica; i++) {
		dbg("\t[%d] priority=%d disabled=%d path=%s pathlen=%d\n", i,
		    config.replicas[i].priority, config.replicas[i].disabled,
		    config.replicas[i].path, config.replicas[i].pathlen);
	}
	dbg("]\n");
}

/*
unsigned int get_file_max()
{
	int res;
	unsigned long long fmax;
#ifdef __linux__
	FILE *tmpf;
#else
	int    oldval;
	size_t oldlenp = sizeof(oldval);
	int    sysctl_names[] = { CTL_KERN, KERN_MAXFILESPERPROC };
#endif

#ifdef __linux__
	if((tmpf = fopen("/proc/sys/fs/nr_open", "r")) != NULL) {
		int res = fscanf(tmpf, "%lld", &fmax);
		fclose(tmpf);
	} else {
		fmax = 4096;
	}
#else
	res = sysctl(sysctl_names, 2, &oldval, &oldlenp, NULL, 0);
	if (res) {
		print_err(errno,"reading system parameter 'max open files'");
		fmax = 4096;
	} else {
		fmax = (long long unsigned int) oldval;
	}
#endif
	return fmax;
}
*/

int do_mount(char *filesystems, char *mountpoint)
{
	unsigned int i;
	char   *t, *token;
	unsigned long tmpfd;
	struct rlimit rlp;

	// Calculate config.max_replica
	// Should also get max_replica_low for exact memory allocation
	t = filesystems;
	for (i=0; t[i]; t[i]=='=' ? i++ : *t++);
	config.max_replica = i + 1;

	/*
	 * Increase process number of file descriptor up to max.
	 * Should detect if root, then use system maximum per process.
	 */
	//config.fd_buf_size = get_file_max();
	//tmpfd = (config.fd_buf_size >>= 1);
	if (getrlimit(RLIMIT_NOFILE, &rlp)) {
		print_err(errno,"reading nofile resource limit");
		exit(errno);
	}
	dbg("file limits: soft = %d hard = %d\n", rlp.rlim_cur, rlp.rlim_max);
	if (rlp.rlim_cur < rlp.rlim_max) {
		dbg("increasing soft limit to hard value\n");
		rlp.rlim_cur = rlp.rlim_max;
		if (!setrlimit(RLIMIT_NOFILE,&rlp)) {
			if (getrlimit(RLIMIT_NOFILE,&rlp)) {
				print_err(errno,"reading nofile resource limit, second attempt");
				exit(errno);
			}
			dbg("new file limits: soft = %d hard = %d\n",
			    rlp.rlim_cur, rlp.rlim_max);
		}
	}
	tmpfd = rlp.rlim_cur;
	tmpfd = (tmpfd - 6) / config.max_replica;
	dbg("max file descriptors = %u\n", tmpfd);

	/* Find closest power-of-two and create mask */
	while (config.fd_buf_size <= tmpfd) {
		config.fd_buf_size <<= 1;
	}
	config.fd_buf_size >>= 1;
	dbg("config.fd_buf_size = %#x\n",config.fd_buf_size);

	// Allocate config.tab_fd
	config.tab_fd.fd = calloc(config.fd_buf_size,sizeof(int *));
	if (!config.tab_fd.fd) {
		print_err(CHIRONFS_ERR_LOW_MEMORY,"file descriptor hash table allocation");
		exit(CHIRONFS_ERR_LOW_MEMORY);
	}
	for(i=0; i < config.fd_buf_size; i++) {
		config.tab_fd.fd[i] = NULL;
	}

	// Allocate config.replicas
	config.replicas = calloc(config.max_replica,sizeof(replica_t));
	if (!config.replicas) {
		print_err(errno,"replica info allocation");
		exit(errno);
	}
	for(i=0;i < config.max_replica;++i) {
		config.replicas[i].path     = NULL;
		config.replicas[i].disabled = 0;
		config.replicas[i].priority = 0;
		config.replicas[i].totrd = 0;
		config.replicas[i].totwr = 0;
	}

	// Allocate round_robin_high
	config.round_robin_high = calloc(config.max_replica,sizeof(int));
	if (!config.round_robin_high) {
		print_err(errno,"high priority round robin table allocation");
		exit(errno);
	}

	// Allocate round_robin_low
	config.round_robin_low = calloc(config.max_replica,sizeof(int));
	if (!config.round_robin_low) {
		print_err(errno,"low priority round robin table allocation");
		exit(errno);
	}

	// Split replica path into config.replicas
	for(i = 0, t = filesystems; ; t = NULL, i++)
	{
		token = strtok(t, "=");
		if(!token)
			break;
		if(token[0] == ':') {
			token += 1;
			config.replicas[i].priority = 1;
			config.round_robin_low[config.max_replica_low++] = i;
		}
		else {
			config.round_robin_high[config.max_replica_high++] = i;
		}
		config.replicas[i].path = realpath(token, NULL);
		if(!config.replicas[i].path) {
			print_err(errno, token);
			exit(errno);
		}
		config.replicas[i].pathlen = strlen(config.replicas[i].path);
		if (config.replicas[i].priority) {
			_log("Replica priority low", config.replicas[i].path, 0);
		} else {
			_log("Replica priority high", config.replicas[i].path, 0);
		}
	}

	dump_config();

	return 0;
}


