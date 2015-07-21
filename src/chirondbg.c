/* Copyright 2005-2008 Luis Furquim
 *
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


#include "config.h"

#ifdef linux
#define __USE_BSD


/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define FUSE_USE_VERSION 25

// 
// The lines below are from a patch contributed by Yen-Ming Lee,
// porting ChironFS to FreeBSD
//
#include <fuse.h>
#if defined(linux) || defined(__FreeBSD__)

#include <fuse/fuse.h>
#include <fuse/fuse_opt.h>

#else

typedef  uint64_t cpuset_t;

// 
// The lines below are from a patch contributed by Antti Kantee
// to make ChironFS run on NetBSD
//

#include <fuse_opt.h>

#endif
//
// End of BSD patches
//

#ifdef __linux__
#define __USE_BSD
#endif

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
#include <libgen.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#ifdef __linux__

#include <linux/limits.h>
#include <mntent.h>
#include <bits/wordsize.h>

#else

#include <limits.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#endif


// 
// The lines below are from a patch contributed by Yen-Ming Lee,
// porting ChironFS to FreeBSD
//
#ifndef HAVE_GETMNTENT
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif
//
// End of BSD patch
//


#include <stdint.h>
#include <pwd.h>
#include <grp.h>


#ifdef __linux__

#define _REENTRANT

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

/* for LinuxThreads */
#define _P __P

#endif

#include <pthread.h>


#include "chiron-types.h"
#define _CHIRONDBG_H_
#include "chironfs.h"
#include "chirondbg.h"


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//
//
//  D E B U G    S T U F F
//
//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#ifdef _DBG_
void debug(const char *s, ...);
int timeval_sub (struct timeval *result, struct timeval *x, struct timeval *y);

void debug(const char *s, ...)
{
   FILE *fd;
   int res;
   int bkerrno = errno;
   va_list ap;

   va_start (ap, s);
   fd  = fopen("/tmp/chironfs-dbg.txt","a");
   res = vfprintf(fd,s,ap);
   fclose(fd);
   va_end (ap);

   errno = bkerrno;
}

/*
   Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.
*/

int timeval_sub (struct timeval *result, struct timeval *x, struct timeval *y)
{
   /* Perform the carry for the later subtraction by updating y. */
   if (x->tv_usec < y->tv_usec) {
      int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
      y->tv_usec -= 1000000 * nsec;
      y->tv_sec += nsec;
   }
   if (x->tv_usec - y->tv_usec > 1000000) {
      int nsec = (x->tv_usec - y->tv_usec) / 1000000;
      y->tv_usec += 1000000 * nsec;
      y->tv_sec -= nsec;
   }
  
   /* Compute the time remaining to wait.
      tv_usec is certainly positive. */
   result->tv_sec = x->tv_sec - y->tv_sec;
   result->tv_usec = x->tv_usec - y->tv_usec;
  
   /* Return 1 if result is negative. */
   return x->tv_sec < y->tv_sec;
}


#endif

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//
//
//  A U X I L I A R Y    F U N C T I O N S
//
//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void print_err(int err, char *specifier)
{
   if (!quiet_mode) {
      if (specifier==NULL) {
         if (err>0) {
            fprintf(stderr,"%s\n",strerror(err));
         } else {
            fprintf(stderr,"%s\n",errtab[-(err+1)]);
         }
      } else {
         if (err>0) {
            fprintf(stderr,"%s: %s\n",specifier,strerror(err));
         } else {
            fprintf(stderr,"%s: %s\n",specifier,errtab[-(err+1)]);
         }
      }
   }
}


void call_log(char *fnname, char *resource, int err)
{
   time_t     t;
   struct tm *ptm;
   char       tmstr[20];

   if (logfd!=NULL) {
      attach_log();
      flockfile(logfd);
      t   = time(NULL);
      ptm = localtime(&t);
      strftime(tmstr,19,"%Y/%m/%d %H:%M ",ptm);
      fputs(tmstr,logfd);
      fputs(fnname,logfd);
      if (err!=CHIRONFS_ADM_FORCED) {
         fputs(" failed accessing ",logfd);
      } else {
         fputs(" ",logfd);
      }
      fputs(resource,logfd);
      if (err) {
         fputs(" ",logfd);
         if (err>0) {
            fputs(strerror(err),logfd);
         } else {
            fputs(errtab[-(err+1)],logfd);
         }
      }
      fputs("\n",logfd);
      fflush(logfd);
      funlockfile(logfd);
   }
}

void attach_log(void)
{
   mode_t tmpmask;
   
   if (logfd) {
      fclose(logfd);
   }
   tmpmask = umask(0133);
   logfd = fopen(logname,"a");
   umask(tmpmask);
   if (logfd==NULL) {
      print_err(CHIRONFS_ERR_BAD_LOG_FILE,logname);
      exit(CHIRONFS_ERR_BAD_LOG_FILE);
   }
   setlinebuf(logfd);
}

