#ifndef CHIRONFS_COMMON_H
#define CHIRONFS_COMMON_H
#include "config.h"

#ifdef __linux__
#define _XOPEN_SOURCE 600
#endif

#define FUSE_USE_VERSION 28

#if defined(linux) || defined(__FreeBSD__)
#else
typedef  uint64_t cpuset_t;
#endif

#include <fuse.h>
#include <fuse_opt.h>

#define CHIRONFS_ERR_LOW_MEMORY        -1
#define CHIRONFS_ERR_LOG_ON_MOUNTPOINT -2
#define CHIRONFS_ERR_BAD_OPTIONS       -3
#define CHIRONFS_ERR_TOO_MANY_FOPENS   -4
#define CHIRONFS_ERR_BAD_LOG_FILE      -5
#define CHIRONFS_INVALID_PATH_MAX      -6
#define CHIRONFS_ADM_FORCED            -7

#endif /* CHIRONFS_COMMON_H */
