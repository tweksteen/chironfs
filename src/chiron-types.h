#ifndef CHIRONFS_TYPES_H
#define CHIRONFS_TYPES_H

#include <time.h>
#include <sys/stat.h>

typedef struct {
   char   *path;
   long    totrd, totwr;
   struct  {
      int    old_rd_avg, curr_rd_avg, curr_rd, old_wr_avg, curr_wr_avg, curr_wr;
      time_t upd;
   } min1, min5, min15, min30, hour1, hour12, day, week, month, year;
   size_t  pathlen;
   time_t  disabled;
   int     priority;
} path_t;

/* 
 * When a file is accessed, the chiron system transparently mirrors that
 * access to its replicas in other filesystems. Since the clients works 
 * with just one file descriptor, we have to store the replicas
 * file descriptors in the table below.
 */

// Structure of the file descriptor replica table
typedef struct FD {
   int **fd;
   int  used;
} fd_t;

/*
 *  These are the file attributes of the pseudo-filesystem
 *  mounted via --ctl (or -c) in order to create a "control
 *  panel" of the mounted filesystem.
 */

 typedef struct ctlfs_entry {
   char               *path;
   struct stat         attr;
   struct ctlfs_entry *ctlfs;
} ctlfs_entry_t;

 typedef struct ctlfs_search {
   struct ctlfs_entry *ctlfs;
   int                 i;
} ctlfs_search_t;

#endif /* CHIRONFS_TYPES_H */
