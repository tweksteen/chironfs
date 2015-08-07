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

#include "debug.h"

struct logger logger;

char *errtab[] = {
	"Low memory",
	"Log file must be outside of the mount point",
	"",
	"Too many opened files",
	"Cannot open the log file",
	"Invalid PATH_MAX definition, check your include files and recompile",
	"Forced by administrator"
};

#ifdef DEBUG
void debug(const char *s, ...)
{
	FILE *fd;
	int bkerrno = errno;
	va_list ap;

	va_start (ap, s);
	fd  = fopen("/tmp/chironfs-dbg.txt","a");
	vfprintf(fd, s, ap);
	fclose(fd);
	va_end (ap);

	errno = bkerrno;
}

/*
   Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.
   */

int timeval_sub(struct timeval *result, struct timeval *x, struct timeval *y)
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
	if (!logger.quiet) {
		if (specifier==NULL) {
			if (err > 0) {
				fprintf(stderr,"%s\n",strerror(err));
			} else {
				fprintf(stderr,"%s\n",errtab[-(err+1)]);
			}
		} else {
			if (err > 0) {
				fprintf(stderr,"%s: %s\n",specifier,strerror(err));
			} else {
				fprintf(stderr,"%s: %s\n",specifier,errtab[-(err+1)]);
			}
		}
	}
}

void _log(char *fnname, char *resource, int err)
{
	time_t     t;
	struct tm *ptm;
	char       tmstr[20];

	if (logger.logfd) {
		flockfile(logger.logfd);
		t   = time(NULL);
		ptm = localtime(&t);
		strftime(tmstr,19,"%Y/%m/%d %H:%M ",ptm);
		fputs(tmstr,logger.logfd);
		fputs(fnname,logger.logfd);
		if (err!=CHIRONFS_ADM_FORCED) {
			fputs(" failed accessing ",logger.logfd);
		} else {
			fputs(" ", logger.logfd);
		}
		fputs(resource,logger.logfd);
		if (err) {
			fputs(" ", logger.logfd);
			if (err>0) {
				fputs(strerror(err),logger.logfd);
			} else {
				fputs(errtab[-(err+1)],logger.logfd);
			}
		}
		fputs("\n",logger.logfd);
		fflush(logger.logfd);
		funlockfile(logger.logfd);
	}
}

void open_log(char *logname)
{
	mode_t tmpmask;

	tmpmask = umask(0133);
	logger.logfd = fopen(logname, "a");
	umask(tmpmask);
	if (!logger.logfd) {
		print_err(errno, logname);
		exit(errno);
	}
	setvbuf(logger.logfd, NULL, _IOLBF, 0);
}

