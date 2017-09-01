dnl **************************************************************
dnl CRUISE AUTOCONF SCRIPT
dnl
dnl Process this file with autoconf to produce a configure script.
dnl You may need to use autoheader as well if changing any DEFINEs

dnl sanity checks, output header, location of scripts used here
AC_INIT([cruise-runtime], [2.x])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADER(cruise-runtime-config.h)
AC_CONFIG_MACRO_DIR([./maint/config/])

#AC_PROG_CC([clang])
AC_PROG_CC([mpicc])

CHECK_NUMA
CHECK_ARCH

AC_ARG_ENABLE(cuserid, 
[  --disable-cuserid       Disables attempted use of cuserid() at run time], 
[if test "x$enableval" = "xno" ; then
    AC_DEFINE(CRUISE_DISABLE_CUSERID, 1, Define if cuserid() should be disabled),
fi]
,)

AC_ARG_ENABLE(ld-preload, 
[  --disable-ld-preload    Disables support for LD_PRELOAD library], 
[if test "x$enableval" = "xno" ; then
    DISABLE_LDPRELOAD="1"
fi]
,)

AC_ARG_ENABLE(st-dev-workaround, 
[  --enable-st-dev-workaround Gather device id from parent directory instead of file], 
[if test "x$enableval" = "xyes" ; then
    AC_DEFINE(__CP_ST_DEV_WORKAROUND, 1, Define if device id should be taken from parent directory rather than file)
    DISABLE_LDPRELOAD="1"
fi]
,)

# checks to see how we can print 64 bit values on this architecture
gt_INTTYPES_PRI

dnl Check byte ordering
AC_C_BIGENDIAN

dnl temporarily set large file flags just for this test; we don't want
dnl it to propagate to the makefile because of zlib bugs
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE"
AC_MSG_CHECKING(for off64_t)
AC_TRY_COMPILE(
    [
    #include <stdio.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <stdarg.h>
    #include <string.h>
    #include <time.h>
    #include <stdlib.h>
    #include <errno.h>
    #include <sys/uio.h>
    #include <pthread.h>
    #include <sys/mman.h>
    #include <search.h>
    #include <assert.h>
    ],
    [ off64_t off; ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_OFF64_T, 1, Define if off64_t type is defined),
    AC_MSG_RESULT(no)
)
CFLAGS="$old_cflags"

AC_CHECK_HEADERS(mntent.h sys/mount.h)

# libc functions wrapped by cruise

CP_WRAPPERS+="-Wl,-wrap,access"
CP_WRAPPERS+=",-wrap,lio_listio"
CP_WRAPPERS+=",-wrap,mkdir"
CP_WRAPPERS+=",-wrap,rmdir"
CP_WRAPPERS+=",-wrap,unlink"
CP_WRAPPERS+=",-wrap,remove"
CP_WRAPPERS+=",-wrap,rename"
CP_WRAPPERS+=",-wrap,truncate"
CP_WRAPPERS+=",-wrap,stat"
CP_WRAPPERS+=",-wrap,__lxstat"
CP_WRAPPERS+=",-wrap,__lxstat64"
CP_WRAPPERS+=",-wrap,__xstat"
CP_WRAPPERS+=",-wrap,__xstat64"

CP_WRAPPERS+=",-wrap,creat"
CP_WRAPPERS+=",-wrap,creat64"
CP_WRAPPERS+=",-wrap,open"
CP_WRAPPERS+=",-wrap,open64"
CP_WRAPPERS+=",-wrap,read"
CP_WRAPPERS+=",-wrap,write"
CP_WRAPPERS+=",-wrap,readv"
CP_WRAPPERS+=",-wrap,writev"
CP_WRAPPERS+=",-wrap,pread"
CP_WRAPPERS+=",-wrap,pread64"
CP_WRAPPERS+=",-wrap,pwrite"
CP_WRAPPERS+=",-wrap,pwrite64"
CP_WRAPPERS+=",-wrap,posix_fadvise"
CP_WRAPPERS+=",-wrap,lseek"
CP_WRAPPERS+=",-wrap,lseek64"
CP_WRAPPERS+=",-wrap,ftruncate"
CP_WRAPPERS+=",-wrap,fsync"
CP_WRAPPERS+=",-wrap,fdatasync"
CP_WRAPPERS+=",-wrap,flock"
CP_WRAPPERS+=",-wrap,mmap"
CP_WRAPPERS+=",-wrap,mmap64"
CP_WRAPPERS+=",-wrap,munmap"
CP_WRAPPERS+=",-wrap,msync"
CP_WRAPPERS+=",-wrap,__fxstat"
CP_WRAPPERS+=",-wrap,__fxstat64"
CP_WRAPPERS+=",-wrap,close"

# FILE* functions
#CP_WRAPPERS+=",-wrap,fclose"
#CP_WRAPPERS+=",-wrap,fflush"
#CP_WRAPPERS+=",-wrap,fopen"
#CP_WRAPPERS+=",-wrap,freopen"
#CP_WRAPPERS+=",-wrap,setbuf"
#CP_WRAPPERS+=",-wrap,setvbuf"
#
#CP_WRAPPERS+=",-wrap,fprintf"
#CP_WRAPPERS+=",-wrap,fscanf"
#CP_WRAPPERS+=",-wrap,vfprintf"
#CP_WRAPPERS+=",-wrap,vfscanf"
#
#CP_WRAPPERS+=",-wrap,fgetc"
#CP_WRAPPERS+=",-wrap,fgets"
#CP_WRAPPERS+=",-wrap,fputc"
#CP_WRAPPERS+=",-wrap,fputs"
#CP_WRAPPERS+=",-wrap,getc"
#CP_WRAPPERS+=",-wrap,putc"
#CP_WRAPPERS+=",-wrap,ungetc"
#
#CP_WRAPPERS+=",-wrap,fread"
#CP_WRAPPERS+=",-wrap,fwrite"
#
#CP_WRAPPERS+=",-wrap,fgetpos"
#CP_WRAPPERS+=",-wrap,fseek"
#CP_WRAPPERS+=",-wrap,fsetpos"
#CP_WRAPPERS+=",-wrap,ftell"
#CP_WRAPPERS+=",-wrap,rewind"
#
#CP_WRAPPERS+=",-wrap,clearerr"
#CP_WRAPPERS+=",-wrap,feof"
#CP_WRAPPERS+=",-wrap,ferror"
#
#CP_WRAPPERS+=",-wrap,fseeko"
#CP_WRAPPERS+=",-wrap,ftello"
#CP_WRAPPERS+=",-wrap,fileno"

# wide character FILE* functions
#CP_WRAPPERS+=",-wrap,fwprintf"
#CP_WRAPPERS+=",-wrap,fwscanf"
#CP_WRAPPERS+=",-wrap,vfwprintf"
#CP_WRAPPERS+=",-wrap,vfwscanf"
#CP_WRAPPERS+=",-wrap,fgetwc"
#CP_WRAPPERS+=",-wrap,fgetws"
#CP_WRAPPERS+=",-wrap,fputwc"
#CP_WRAPPERS+=",-wrap,fputws"
#CP_WRAPPERS+=",-wrap,fwide"
#CP_WRAPPERS+=",-wrap,getwc"
#CP_WRAPPERS+=",-wrap,putwc"
#CP_WRAPPERS+=",-wrap,ungetwc"

# ,-u,__wrap___fxstat64,-u,pthread_mutex_lock,-u,pthread_mutex_unlock

# We need to know the value of the $libdir and $bindir variables so that 
# we can reference the correct path in the cruise compiler wrappers.  
# Unfortunately, those two variables are not normally evaluated by autoconf.  
# They are evaluated at build time using Makefile variable substitutions.  
# 
# The following logic was copied from mpich2 1.3.1 to resolve the $libdir
# variable at configure time.
#
# Temporarily replace the default NONE value for exec_prefix
# and prefix with the actual, default values.
savePrefix=$prefix
saveExecprefix=$exec_prefix
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix=$prefix
eval cruise_lib_path=$libdir
eval cruise_bin_path=$bindir
prefix=$savePrefix
exec_prefix=$saveExecprefix

CFLAGS="$old_cflags"
LIBS="$old_libs"

AC_SUBST(cruise_lib_path)
AC_SUBST(cruise_bin_path)
AC_SUBST(LDFLAGS)
AC_SUBST(__CP_LOG_PATH)
AC_SUBST(CP_WRAPPERS)
AC_SUBST(DISABLE_LDPRELOAD)
AC_OUTPUT(Makefile
cruise-config
)


AC_MSG_RESULT([

    =========================
    CRUISE Checkpoint-Restart
    =========================
    prefix      ${prefix}
    compiler    ${CC}
    CFLAGS      ${CFLAGS}
    =========================
    
    Supported POSIX wrappers:
    
    ${CP_WRAPPERS}

])
