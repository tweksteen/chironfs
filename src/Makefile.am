AM_CFLAGS = -Wall

bin_PROGRAMS = chironfs chironctl
chironfs_SOURCES = fs.c conf.c debug.c hash.c
chironfs_CFLAGS = $(CFLAGS) $(FUSE_CFLAGS)
chironfs_LDADD = $(FUSE_LIBS)
chironctl_SOURCES = ctl.c
chironctl_CFLAGS = $(CFLAGS) $(FUSE_CFLAGS)
