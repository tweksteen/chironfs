AM_CFLAGS= $(all_includes) -Wall -W -Wmissing-prototypes -g -O2 -DFUSE_USE_VERSION=25 -D_FILE_OFFSET_BITS=64 -I/usr/local/include

bin_PROGRAMS              = chironfs chirctl
chironfs_SOURCES = chironfs.c chiron-conf.c chirondbg.c chironfs.h chiron-types.h chironfn.c chironfn.h chironoper.h
chirctl_SOURCES = chirctl.c chirondbg.c chirctl.h chironfs.h chiron-types.h chironfn.c chironfn.h
if FREEBSD
OSLD=-pthread -liconv -lfuse -L/usr/local/lib
else
if NETBSD
OSLD=-lrefuse
else
OSLD=-lfuse
endif
endif
chironfs_LDFLAGS = -lm ${OSLD}
chironfs_OBJSS = chironfs.o chiron-conf.o chirondbg.o chironfn.o
chirctl_LDFLAGS = -lm ${OSLD}
chirctl_OBJSS = chirctl.o chirondbg.o chironfn.o
