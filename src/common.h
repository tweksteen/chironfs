#ifndef CHIRONFS_COMMON_H
#define CHIRONFS_COMMON_H
#include "config.h"

#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#define FUSE_USE_VERSION 25

#if defined(linux) || defined(__FreeBSD__)
#else
typedef  uint64_t cpuset_t;
#endif

#include <fuse.h>
#include <fuse_opt.h>

#endif /* CHIRONFS_COMMON_H */
