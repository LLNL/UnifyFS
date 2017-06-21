#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([burstfsd], [0.1], [tw15g@my.fsu.edu])
AM_INIT_AUTOMAKE(burstfsd, 0, 1)
AC_CONFIG_SRCDIR([burstfs_setup.h])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC([mpicc])
AC_ARG_WITH(mdhim, [--with-mdhim=PATH specify path to mdhim], [CPPFLAGS="-I${withval}/src -I${withval}/Mlog -I${withval}/src/uthash ${CPPFLAGS}"; LDFLAGS="-L${withval}/src -lmdhim ${LDFLAGS}"])
AC_ARG_WITH(leveldb, [--with-leveldb=PATH specify path to mdhim], [CPPFLAGS="-I${withval}/include ${CPPFLAGS}"; LDFLAGS="-L${withval} -lleveldb ${LDFLAGS}"])
# Checks for libraries.
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [main])
# FIXME: Replace `main' with a function in `-lmdhim':
AC_CHECK_LIB([mdhim], [main])
# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main])
# FIXME: Replace `main' with a function in `-lrt':
AC_CHECK_LIB([rt], [main])
AC_CHECK_LIB([stdc++], [main])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_CHECK_FUNCS([ftruncate getpagesize gettimeofday memset socket])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
