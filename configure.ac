dnl
dnl This file is a part of UnifyFS. Please see LICENSE for the license
dnl information.
dnl Process this file with autoconf to produce a configure script.

AC_LANG([C])

AC_INIT([unifyfs],
        m4_esyscmd([git describe --always |
                   awk '/.*/{sub(/^v/,""); printf "%s",$1; exit}']),
        [unifycr@llnl.gov])
AC_PREREQ(2.60)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign tar-pax subdir-objects])
AM_SILENT_RULES([yes])

AM_MAINTAINER_MODE([disable])

AC_PROG_CC_STDC
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# fortran support
AC_ARG_ENABLE([fortran],[AS_HELP_STRING([--enable-fortran],[Enable fortran compatibility and features])])
AC_MSG_CHECKING(if fortran is wanted )
AS_IF([test "x$enable_fortran" = "xyes"],[
    AC_MSG_RESULT(yes)
    AC_PROG_FC
    AM_CONDITIONAL([HAVE_FORTRAN], [true])
],[
    AC_MSG_RESULT(no)
    AM_CONDITIONAL([HAVE_FORTRAN], [false])
])

dnl Need to do Fortran checks before initializing LIBTOOL
LT_INIT

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_C_INLINE
AC_C_RESTRICT
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_CHECK_TYPES([ptrdiff_t])

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h stdlib.h string.h unistd.h])
AC_CHECK_HEADERS([fcntl.h inttypes.h libgen.h limits.h mntent.h strings.h syslog.h])
AC_CHECK_HEADERS([wchar.h wctype.h])
AC_CHECK_HEADERS([sys/mount.h sys/socket.h sys/statfs.h sys/time.h])
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h])

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([ftruncate getpagesize gettimeofday memset socket floor])
AC_CHECK_FUNCS([gethostbyname strcasecmp strdup strerror strncasecmp strrchr])
AC_CHECK_FUNCS([gethostname strstr strtoumax strtol uname posix_fallocate])

# PMPI Init/Fini mount/unmount option
AC_ARG_ENABLE([mpi-mount],[AS_HELP_STRING([--enable-mpi-mount],[Enable transparent mount/unmount at MPI_Init/Finalize.])])
AS_IF([test "x$enable_mpi_mount" = "xyes"],[
    AM_CONDITIONAL([USE_PMPI_WRAPPERS], [true])
],[
    AM_CONDITIONAL([USE_PMPI_WRAPPERS], [false])
])

# PMIx support build option
AC_ARG_ENABLE([pmix],[AS_HELP_STRING([--enable-pmix],[Enable PMIx build options.])])
AS_IF([test "x$enable_pmix" = "xyes"],[
    AC_CHECK_HEADERS([pmix.h],
                     [AM_CONDITIONAL([USE_PMIX], [true])],
                     [AM_CONDITIONAL([USE_PMIX], [false])])
],[
    AM_CONDITIONAL([USE_PMIX], [false])
])

# PMI2 support build option
AC_ARG_ENABLE([pmi],[AS_HELP_STRING([--enable-pmi],[Enable PMI2 build options.])])
AS_IF([test "x$enable_pmi" = "xyes"],[
    AC_CHECK_HEADERS([pmi2.h],
                     [AM_CONDITIONAL([USE_PMI2], [true])],
                     [AM_CONDITIONAL([USE_PMI2], [false])])
],[
    AM_CONDITIONAL([USE_PMI2], [false])
])

# MDHIM support build option
AC_ARG_ENABLE([mdhim],[AS_HELP_STRING([--enable-mdhim],[Enable MDHIM build options.])])
AS_IF([test "x$enable_mdhim" = "xyes"],[
    AM_CONDITIONAL([USE_MDHIM], [true])
],[
    AM_CONDITIONAL([USE_MDHIM], [false])
])

AC_ARG_WITH(pkgconfigdir,
    [AS_HELP_STRING([--with-pkgconfigdir=DIR],[pkgconfig file in DIR @<:@LIBDIR/pkgconfig@:>@])],
            [pkgconfigdir=$withval],
            [pkgconfigdir='${libdir}/pkgconfig'])
AC_SUBST(pkgconfigdir)

## unifyfs options
AC_ARG_ENABLE([cuserid],[AS_HELP_STRING([--disable-cuserid],[Disables attempted use of cuserid() at run time])],[
        AS_IF([test "x$enableval" = "xno"],[
            AC_DEFINE(CRUISE_DISABLE_CUSERID, 1, Define if cuserid() should be disabled),],[])]
,)

AC_ARG_ENABLE(ld-preload,[AS_HELP_STRING([--disable-ld-preload],[Disables support for LD_PRELOAD library])],[
        AS_IF([test "x$enableval" = "xno"],[
            DISABLE_LDPRELOAD="1"
        ],[])
    ]
,[])

AC_ARG_ENABLE(st-dev-workaround,
    [AS_HELP_STRING([--enable-st-dev-workaround],[Gather device id from parent directory instead of file])],[
        AS_IF([test "x$enableval" = "xyes"],[
            AC_DEFINE(__CP_ST_DEV_WORKAROUND, 1, Define if device id should be taken from parent directory rather than file)
            DISABLE_LDPRELOAD="1"
        ],[])
    ]
,[])

# use mpi for bootstraping unifyfsd
AC_ARG_ENABLE(unifyfsd-mpi,
    [AS_HELP_STRING([--enable-unifyfsd-mpi],[Use MPI for bootstrapping unifyfsd])],[
        AS_IF([test "x$enableval" = "xyes"],[
            AC_DEFINE(UNIFYFSD_USE_MPI, 1, Define if unifyfsd has to use mpi for bootstrapping)],[])
    ]
,[])


# look for MPI and set flags
LX_FIND_MPI
AS_IF([test "x$enable_fortran" = "xyes"],[
    AC_LANG_PUSH([Fortran])
    LX_FIND_MPI
    AC_LANG_POP
],[])

AS_IF([test "$have_C_mpi" != "yes"],
    AC_MSG_ERROR(["Couldn't find MPI"]),
    []
)

# look for leveldb library, sets LEVELDB_CFLAGS/LDFLAGS/LIBS
UNIFYFS_AC_LEVELDB

# look for gotcha library, sets GOTCHA_INCLUDE, GOTCHA_LIB
UNIFYFS_AC_GOTCHA

# look for spath library, sets SPATH_CFLAGS, SPATH_LDFLAGS, SPATH_LIBS
UNIFYFS_AC_SPATH

UNIFYFS_AC_MARGO
UNIFYFS_AC_FLATCC

# openssl for md5 checksum
UNIFYFS_AC_OPENSSL

# checks to see how we can print 64 bit values on this architecture
gt_INTTYPES_PRI

dnl Check byte ordering
AC_C_BIGENDIAN

dnl temporarily set large file flags just for this test; we don't want
dnl it to propagate to the makefile because of zlib bugs
AC_MSG_CHECKING(for off64_t)
AC_TRY_COMPILE(
    [
    #define _FILE_OFFSET_BITS 64
    #define _LARGEFILE64_SOURCE
    #include <sys/types.h>
    ],
    [ off64_t off; ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_OFF64_T, 1, Define if off64_t type is defined),
    AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(if linker supports -wrap)
OLD_LDFLAGS=$LDFLAGS
LDFLAGS=$LDFLAGS
LDFLAGS+="-Wl,-wrap,malloc"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdlib.h>]],[[void* __wrap_malloc(size_t size);]],[[int *test = malloc(sizeof(int));]])],
[
    AC_MSG_RESULT([yes])
    AM_CONDITIONAL([HAVE_LD_WRAP],[true])
],[
    AC_MSG_RESULT([no])
    AM_CONDITIONAL([HAVE_LD_WRAP],[false])
])
LDFLAGS=$OLD_LDFLAGS


# HDF found?
AX_LIB_HDF5
AM_CONDITIONAL([HAVE_HDF5], [test x$with_hdf5 = xyes])

# libc functions wrapped by unifyfs

CP_WRAPPERS+="-Wl,-wrap,access"
CP_WRAPPERS+=",-wrap,chmod"
CP_WRAPPERS+=",-wrap,fchmod"

OLD_LIBS=$LIBS
LIBS+="-lrt"
AC_CHECK_FUNCS(lio_listio,[
    CP_WRAPPERS+=",-wrap,lio_listio"
], [])
LIBS=$OLD_LIBS

CP_WRAPPERS+=",-wrap,mkdir"
CP_WRAPPERS+=",-wrap,rmdir"
CP_WRAPPERS+=",-wrap,chdir"
CP_WRAPPERS+=",-wrap,__getcwd_chk"
CP_WRAPPERS+=",-wrap,getcwd"
CP_WRAPPERS+=",-wrap,getwd"
CP_WRAPPERS+=",-wrap,get_current_dir_name"
CP_WRAPPERS+=",-wrap,unlink"
CP_WRAPPERS+=",-wrap,remove"
CP_WRAPPERS+=",-wrap,rename"
CP_WRAPPERS+=",-wrap,truncate"
CP_WRAPPERS+=",-wrap,stat"
CP_WRAPPERS+=",-wrap,fstat"


AC_CHECK_FUNCS(statfs,[
    CP_WRAPPERS+=",-wrap,statfs"
],[])
AC_CHECK_FUNCS(fstatfs,[
    CP_WRAPPERS+=",-wrap,fstatfs"
],[])

AC_CHECK_FUNCS(__lxstat,[
    CP_WRAPPERS+=",-wrap,__lxstat"
],[])
AC_CHECK_FUNCS(__xstat,[
    CP_WRAPPERS+=",-wrap,__xstat"
],[])
AC_CHECK_FUNCS(__fxstat,[
    CP_WRAPPERS+=",-wrap,__fxstat"
],[])

CP_WRAPPERS+=",-wrap,creat"
CP_WRAPPERS+=",-wrap,creat64"
CP_WRAPPERS+=",-wrap,open"
AC_CHECK_FUNCS(open64, [
    CP_WRAPPERS+=",-wrap,open64"
],[])
CP_WRAPPERS+=",-wrap,__open_2"
CP_WRAPPERS+=",-wrap,read"
CP_WRAPPERS+=",-wrap,write"
CP_WRAPPERS+=",-wrap,readv"
CP_WRAPPERS+=",-wrap,writev"
CP_WRAPPERS+=",-wrap,pread"
CP_WRAPPERS+=",-wrap,pread64"
CP_WRAPPERS+=",-wrap,pwrite"
CP_WRAPPERS+=",-wrap,pwrite64"
AC_CHECK_FUNCS(posix_fadvise, [
    CP_WRAPPERS+=",-wrap,posix_fadvise"
],[])
CP_WRAPPERS+=",-wrap,lseek"
CP_WRAPPERS+=",-wrap,lseek64"
CP_WRAPPERS+=",-wrap,fchdir"
CP_WRAPPERS+=",-wrap,ftruncate"
CP_WRAPPERS+=",-wrap,fsync"
CP_WRAPPERS+=",-wrap,fdatasync"
CP_WRAPPERS+=",-wrap,flock"
CP_WRAPPERS+=",-wrap,mmap"
CP_WRAPPERS+=",-wrap,mmap64"
CP_WRAPPERS+=",-wrap,munmap"
CP_WRAPPERS+=",-wrap,msync"
CP_WRAPPERS+=",-wrap,close"

# FILE* functions
CP_WRAPPERS+=",-wrap,fclose"
CP_WRAPPERS+=",-wrap,fflush"
CP_WRAPPERS+=",-wrap,fopen"
CP_WRAPPERS+=",-wrap,freopen"
CP_WRAPPERS+=",-wrap,setbuf"
CP_WRAPPERS+=",-wrap,setvbuf"

CP_WRAPPERS+=",-wrap,fprintf"
CP_WRAPPERS+=",-wrap,fscanf"
CP_WRAPPERS+=",-wrap,vfprintf"
CP_WRAPPERS+=",-wrap,vfscanf"

CP_WRAPPERS+=",-wrap,fgetc"
CP_WRAPPERS+=",-wrap,fgets"
CP_WRAPPERS+=",-wrap,fputc"
CP_WRAPPERS+=",-wrap,fputs"
CP_WRAPPERS+=",-wrap,getc"
CP_WRAPPERS+=",-wrap,putc"
CP_WRAPPERS+=",-wrap,ungetc"

CP_WRAPPERS+=",-wrap,fread"
CP_WRAPPERS+=",-wrap,fwrite"

CP_WRAPPERS+=",-wrap,fgetpos"
CP_WRAPPERS+=",-wrap,fseek"
CP_WRAPPERS+=",-wrap,fsetpos"
CP_WRAPPERS+=",-wrap,ftell"
CP_WRAPPERS+=",-wrap,rewind"

CP_WRAPPERS+=",-wrap,clearerr"
CP_WRAPPERS+=",-wrap,feof"
CP_WRAPPERS+=",-wrap,ferror"

CP_WRAPPERS+=",-wrap,fseeko"
CP_WRAPPERS+=",-wrap,ftello"
CP_WRAPPERS+=",-wrap,fileno"

# wide character FILE* functions
CP_WRAPPERS+=",-wrap,fwprintf"
CP_WRAPPERS+=",-wrap,fwscanf"
CP_WRAPPERS+=",-wrap,vfwprintf"
CP_WRAPPERS+=",-wrap,vfwscanf"
CP_WRAPPERS+=",-wrap,fgetwc"
CP_WRAPPERS+=",-wrap,fgetws"
CP_WRAPPERS+=",-wrap,fputwc"
CP_WRAPPERS+=",-wrap,fputws"
CP_WRAPPERS+=",-wrap,fwide"
CP_WRAPPERS+=",-wrap,getwc"
CP_WRAPPERS+=",-wrap,putwc"
CP_WRAPPERS+=",-wrap,ungetwc"

# ,-u,__wrap___fxstat64,-u,pthread_mutex_lock,-u,pthread_mutex_unlock

# We need to know the value of the $libdir and $bindir variables so that
# we can reference the correct path in the unifyfs compiler wrappers.
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
eval unifyfs_lib_path=$libdir
eval unifyfs_bin_path=$bindir
prefix=$savePrefix
exec_prefix=$saveExecprefix

# autoconf<=2.69, runstatedir is not configurable.
if test "x$runstatedir" = x; then
        AC_SUBST([runstatedir], ['${localstatedir}/run'])
fi

AC_SUBST(unifyfs_lib_path)
AC_SUBST(unifyfs_bin_path)
AC_SUBST(LDFLAGS)
AC_SUBST(__CP_LOG_PATH)
AC_SUBST(CP_WRAPPERS)
AC_SUBST(DISABLE_LDPRELOAD)

AC_CONFIG_FILES([Makefile
                 common/Makefile
                 common/src/Makefile
                 meta/Makefile
                 meta/src/Makefile
                 server/Makefile
                 server/src/Makefile
                 client/Makefile
                 client/src/Makefile
                 client/unifyfs.pc
                 examples/Makefile
                 examples/src/Makefile
                 extras/Makefile
                 extras/unifyfs.conf
                 t/Makefile
                 t/lib/Makefile
                 util/Makefile
                 util/scripts/Makefile
                 util/scripts/lsfcsm/Makefile
                 util/unifyfs/Makefile
                 util/unifyfs/src/Makefile
                 util/unifyfs-stage/Makefile
                 util/unifyfs-stage/src/Makefile])

AC_CONFIG_FILES([client/unifyfs-config], [chmod +x client/unifyfs-config])
AC_CONFIG_FILES([util/scripts/lsfcsm/unifyfs_lsfcsm_prolog], [chmod +x util/scripts/lsfcsm/unifyfs_lsfcsm_prolog])
AC_CONFIG_FILES([util/scripts/lsfcsm/unifyfs_lsfcsm_epilog], [chmod +x util/scripts/lsfcsm/unifyfs_lsfcsm_epilog])

UNIFYFS_VERSION=${PACKAGE_VERSION}
AC_SUBST(UNIFYFS_VERSION)
AC_SUBST([LIBUNIFYFS_LT_VERSION], [1:0:0])

# pkgconfig versioning
LIBUNIFYFS_API_VERSION="1.0.0"
AC_SUBST(LIBUNIFYFS_API_VERSION)

AC_OUTPUT

AC_MSG_RESULT([

    ==========================
    UNIFYFS
    ==========================
    prefix      ${prefix}
    compiler    ${CC}
    CFLAGS      ${CFLAGS}
    ==========================

    Supported POSIX wrappers:

    ${CP_WRAPPERS}

])
