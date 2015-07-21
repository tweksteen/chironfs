
#define min(a,b) (((a) < (b)) ? (a) : (b))


#if defined(_CHIRONFN_H_)
char    *currdir            = ".";
#else
extern char    *currdir;
#endif


int read_a_line(char **buf, int *c, FILE *f);
char *xlate(const char *fname, char *rpath);
