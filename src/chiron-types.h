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

#ifndef CHIRONFS_TYPES_H
#define CHIRONFS_TYPES_H

#include <time.h>
#include <sys/stat.h>

typedef struct {
   char   *path;
   unsigned long long totrd, totwr;
   /*
   struct {
      int    old_rd_avg, curr_rd_avg, curr_rd, old_wr_avg, curr_wr_avg, curr_wr;
      time_t upd;
   } min1, min5, min15, min30, hour1, hour12, day, week, month, year; */
   size_t  pathlen;
   int     disabled;
   int     priority;
} replica_t;

/*
 * When a file is accessed, the chiron system transparently mirrors that
 * access to its replicas in other filesystems. Since the clients works
 * with just one file descriptor, we have to store the replicas
 * file descriptors in the table below.
 */

// Structure of the file descriptor replica table
typedef struct {
   int **fd;
   int  used;
} fd_t;

#endif /* CHIRONFS_TYPES_H */
