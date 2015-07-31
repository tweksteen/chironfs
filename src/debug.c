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

#include "debug.h"

char *errtab[] = {
   "Low memory",
   "Log file must be outside of the mount point",
   "",
   "Too many opened files",
   "Cannot open the log file",
   "Invalid PATH_MAX definition, check your include files and recompile",
   "Forced by administrator"
};
FILE    *logfd              = NULL;
int      quiet_mode         = 0;
char    *logname            = NULL;

#ifdef DEBUG
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
   setvbuf(logfd, NULL, _IOLBF, 0);
}

