#include "utils.h"

char *currdir = ".";

int read_a_line(char **buf, int *c, FILE *f)
{
	char *ret;
	int  sz, someerr, pos;

	if (*buf) {
		free(*buf);
		(*buf) = NULL;
	}

	someerr = fscanf(f, "%4X", &sz);
	if (someerr != 1) {
		return -1;
	}

	(*buf) = malloc((sz+1) * sizeof(char));

	if (!(*buf)) {
		return -1;
	}

	pos = 0;
	do {
		ret = fgets((*buf)+pos,sz+1-pos,f);
	} while (ret && ((pos+=strlen(*buf))!=sz));

	if (!ret) {
		free(*buf);
		(*buf) = NULL;
		return -1;
	}

	(*c) = sz;
	return 0;
}
