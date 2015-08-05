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

#ifndef CHIRONFS_CTL_H
#define CHIRONFS_CTL_H

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include <sys/statvfs.h>

#include <sys/stat.h>

#include <stdint.h>
#include <pwd.h>
#include <grp.h>

#include <pthread.h>

#include "chiron-types.h"
#include "debug.h"
#include "utils.h"

extern ctlfs_entry_t  ctlfs[];
extern struct          fuse_operations chironctl_oper;
extern int             max_replica;
extern char           *mount_point;
extern char           *chironctl_mountpoint;
extern replica_t         *replicas;
extern unsigned long   inode_count;
extern char           *chironctl_parentdir;
extern FILE           *tochironfs, *fromchironfs;
extern pthread_mutex_t comm;
extern char           status_fname[];
extern char           nagios_fname[];
extern char           nagios_script[];

int mkctlfs(void);
ctlfs_entry_t mkstatnod(char *path, unsigned long mode, unsigned short uid, unsigned short gid);
ctlfs_search_t find_path(const char *path, ctlfs_entry_t *c, int deep);
void free_ctlnode(ctlfs_entry_t *ctlroot);
void free_vars(void);
int get_perm(uid_t uid, gid_t gid, struct stat st);
int get_path_perm(const char *path);
char *get_daddy(const char *path);

#endif /* CHIRONFS_CTL_H */
