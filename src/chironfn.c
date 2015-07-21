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
#include <sys/stat.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <sys/types.h>
#ifdef __linux__

#include <linux/limits.h>
#include <mntent.h>
#include <bits/wordsize.h>

#else

#include <limits.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#endif

#include "chirondbg.h"
#define _CHIRONFN_H_
#include "chironfn.h"

int read_a_line(char **buf, int *c, FILE *f)
{
   char *ret;
   int  sz, someerr, pos;

   if ((*buf) != NULL) {
      free(*buf);
      (*buf) = NULL;
   }

   someerr = fscanf(f, "%4X", &sz);
   if (someerr!=1) {
      return(-1);
   }

   (*buf) = malloc((sz+1) * sizeof(char));

   if ((*buf) == NULL) {
      // no mem
      return(-1);
   }

   pos = 0;
   do {
      ret = fgets((*buf)+pos,sz+1-pos,f);
   } while ((ret!=NULL) && ((pos+=strlen(*buf))!=sz));
   
   if (ret==NULL) {
      // error
      free(*buf);
      (*buf) = NULL;
      return(-1);
   }

   (*c) = sz;

   return(0);
}

char *xlate(const char *fname, char *rpath)
{
   char *rname;
   int   rlen, flen;

   if ((rpath==NULL)||(fname==NULL)) {
      return(NULL);
   }

   if (!strcmp(rpath,".")) {
      flen = strlen(fname);
      rname = malloc(1+flen);
      if (rname!=NULL) {
         if (!strcmp(fname,"/")) {
            strcpy(rname,currdir);
         } else {
            strcpy(rname,fname+1);
         }
      }
   } else {
      rlen = strlen(rpath);
      flen = strlen(fname);
      rname = malloc(1+rlen+flen);
      if (rname!=NULL) {
         strcpy(rname,rpath);
         strcpy(rname+rlen,fname);
      }
   }
                                                dbg(("\nxlate:%s",rname));
   return(rname);
}

