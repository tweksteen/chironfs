#ifdef _DBG_

#define dbg(param) debug param
#define timeval_subtract(res, x, y) timeval_sub(res, x, y)
#define gettmday(t,p) gettimeofday(t,p)
#define decl_tmvar(a,b,c) struct timeval a, b, c

void debug(const char *s, ...);
int timeval_sub (struct timeval *result, struct timeval *x, struct timeval *y);

#else

#define dbg(param)
#define timeval_subtract(res, x, y)
#define gettmday(t,p)
#define decl_tmvar(a,b,c)

#endif

void print_err(int err, char *specifier);
void call_log(char *fnname, char *resource, int err);
void attach_log(void);

#if defined(_CHIRONDBG_H_)

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

#else

extern char                   *errtab[];
extern FILE    *logfd;
extern int      quiet_mode;
extern char    *logname;

#endif

#define CHIRONFS_ERR_BAD_LOG_FILE      -5

