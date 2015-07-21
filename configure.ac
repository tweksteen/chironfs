#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([chironfs], [1.1.1], [luisfurquim@gmail.com])
AC_CONFIG_SRCDIR([src/chironoper.h])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB(pthread, pthread_create)

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h mntent.h stdint.h stdlib.h string.h sys/mount.h sys/param.h sys/statvfs.h sys/time.h unistd.h pthread.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_HEADER_TIME
AC_STRUCT_TM
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_FORK
AC_FUNC_GETMNTENT
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([dup2 getcwd getmntent getmntinfo lchown localtime_r memset mkdir mkfifo realpath rmdir strdup strerror strrchr utime])

AC_CONFIG_FILES([Makefile] [src/Makefile] [doc/Makefile] [man/Makefile])
AC_CANONICAL_TARGET
case $host in
     *-*-freebsd*)
       os_type=freebsd
        ;;
     *-*-pcbsd*)
       os_type=freebsd
        ;;
     *-*-netbsd*)
       os_type=netbsd
        ;;
    *)
        os_type=other
        ;;
esac
AM_CONDITIONAL(FREEBSD,test "x$os_type" = "xfreebsd")
AM_CONDITIONAL(NETBSD,test "x$os_type" = "xnetbsd")
AC_DEFINE(PACKAGE, [chironfs], [Package Name])
AC_DEFINE(VERSION, [1.1.1], [Package Version])
AC_OUTPUT
