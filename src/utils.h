#ifndef CHIRONFS_UTILS_H
#define CHIRONFS_UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))

extern char    *currdir;

int read_a_line(char **buf, int *c, FILE *f);

#endif /* CHIRONFS_UTILS_H */
