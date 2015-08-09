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

/* For asprintf */
#define _GNU_SOURCE

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
#include <libgen.h>
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

typedef struct ctlfs_entry {
   char                *path;
   struct stat          attr;
   struct ctlfs_entry **children;
   unsigned int		n_children;
} ctlfs_entry_t;

typedef struct {
   struct ctlfs_entry *ctlfs;
   int                 i;
} ctlfs_search_t;

struct chironctl_config {
	ctlfs_entry_t   *root;
	struct          fuse_operations chironctl_oper;
	int             max_replica;
	char           *mountpoint;
	char           *chironctl_mountpoint;
	replica_t      *replicas;
	unsigned long   inode_count;
	char           *chironctl_parentdir;
	FILE           *to_chironfs, *from_chironfs;
	pthread_mutex_t mutex;
};

extern struct chironctl_config config;

#define STATUS_FNAME "status"

int mkctlfs(void);
ctlfs_entry_t *mkstatnod(char *, unsigned long, uid_t, gid_t);
ctlfs_search_t find_path(const char *path, ctlfs_entry_t *c, int deep);
int get_perm(uid_t uid, gid_t gid, struct stat st);
int get_path_perm(const char *path);
char *get_daddy(const char *path);

#endif /* CHIRONFS_CTL_H */
