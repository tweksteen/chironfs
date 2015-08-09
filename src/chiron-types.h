#ifndef CHIRONFS_TYPES_H
#define CHIRONFS_TYPES_H

#include <time.h>
#include <sys/stat.h>

typedef struct {
   char   *path;
   long    totrd, totwr;
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
