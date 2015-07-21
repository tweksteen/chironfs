AM_CFLAGS= $(all_includes) -Wall -W -Wmissing-prototypes -g -O2 -DFUSE_USE_VERSION=25 -D_FILE_OFFSET_BITS=64

bin_PROGRAMS              = chironfs
chironfs_SOURCES        = chironfs.c chiron-conf.c chironfs.h
if FREEBSD
FREEBSDLD = -pthread -liconv 
else
FREEBSDLD =
endif
chironfs_LDFLAGS        = -lfuse -lm ${FREEBSDLD}
chironfs_OBJS           = chironfs.o chiron-conf.o
