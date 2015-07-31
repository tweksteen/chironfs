#include "utils.h"

char *currdir = ".";

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
	dbg("\nxlate:%s",rname);
	return(rname);
}

