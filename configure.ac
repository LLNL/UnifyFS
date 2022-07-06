dnl
dnl This file is a part of UnifyFS. Please see LICENSE for the license
dnl information.
dnl Process this file with autoconf to produce a configure script.

AC_LANG([C])

AC_INIT([unifyfs],
        m4_esyscmd([git describe --always |
                   awk '/.*/{sub(/^v/,""); printf "%s",$1; exit}']),
        [ecp-unifyfs@exascaleproject.org])
AC_PREREQ(2.60)
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign tar-pax 1.15])
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
AC_CHECK_HEADER([sys/sysmacros.h], [], AC_MSG_ERROR([cannot find required header sys/sysmacros.h]))

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([ftruncate gettimeofday memset socket floor])
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
    UNIFYFS_AC_LEVELDB
],[
    AM_CONDITIONAL([USE_MDHIM], [false])
])

AC_ARG_WITH(pkgconfigdir,
    [AS_HELP_STRING([--with-pkgconfigdir=DIR],[pkgconfig file in DIR @<:@LIBDIR/pkgconfig@:>@])],
            [pkgconfigdir=$withval],
            [pkgconfigdir='${libdir}/pkgconfig'])
AC_SUBST(pkgconfigdir)

## unifyfs options

AC_ARG_ENABLE(preload,[AS_HELP_STRING([--enable-preload],[Enables support for LD_PRELOAD library])])
AS_IF([test "x$enable_preload" = "xyes"],[
    AM_CONDITIONAL([ENABLE_LD_PRELOAD],[true])
],[
    AM_CONDITIONAL([ENABLE_LD_PRELOAD],[false])
])

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

# look for gotcha library, sets GOTCHA_CFLAGS, GOTCHA_LDFLAGS, GOTCHA_LIBS
UNIFYFS_AC_GOTCHA

# error out if fortran was enabled but GOTCHA wasn't found
AM_COND_IF([HAVE_FORTRAN],[
    AM_COND_IF([HAVE_GOTCHA],[],[
        AC_MSG_ERROR([gotcha required when fortran is enabled])
])],[])

# look for spath library, sets SPATH_CFLAGS, SPATH_LDFLAGS, SPATH_LIBS
UNIFYFS_AC_SPATH

# look for margo library, sets MARGO_CFLAGS, MARGO_LIBS
UNIFYFS_AC_MARGO

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
LDFLAGS+=" -Wl,-wrap,malloc"
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

LINK_WRAPPERS="-Wl"

# path functions
LINK_WRAPPERS+=",-wrap,access"
LINK_WRAPPERS+=",-wrap,chmod"
LINK_WRAPPERS+=",-wrap,creat"
LINK_WRAPPERS+=",-wrap,creat64"
LINK_WRAPPERS+=",-wrap,__open_2"
LINK_WRAPPERS+=",-wrap,open"
AC_CHECK_FUNCS(open64, [
    LINK_WRAPPERS+=",-wrap,open64"
],[])
LINK_WRAPPERS+=",-wrap,remove"
LINK_WRAPPERS+=",-wrap,rename"
LINK_WRAPPERS+=",-wrap,truncate"
LINK_WRAPPERS+=",-wrap,unlink"

# file descriptor functions
LINK_WRAPPERS+=",-wrap,close"
LINK_WRAPPERS+=",-wrap,dup"
LINK_WRAPPERS+=",-wrap,dup2"
LINK_WRAPPERS+=",-wrap,fchmod"
LINK_WRAPPERS+=",-wrap,fdatasync"
LINK_WRAPPERS+=",-wrap,flock"
LINK_WRAPPERS+=",-wrap,fsync"
LINK_WRAPPERS+=",-wrap,ftruncate"
LINK_WRAPPERS+=",-wrap,lseek"
LINK_WRAPPERS+=",-wrap,lseek64"
LINK_WRAPPERS+=",-wrap,pread"
LINK_WRAPPERS+=",-wrap,pread64"
LINK_WRAPPERS+=",-wrap,pwrite"
LINK_WRAPPERS+=",-wrap,pwrite64"
LINK_WRAPPERS+=",-wrap,read"
LINK_WRAPPERS+=",-wrap,readv"
LINK_WRAPPERS+=",-wrap,write"
LINK_WRAPPERS+=",-wrap,writev"

OLD_LIBS=$LIBS
LIBS+=" -lrt"
AC_CHECK_FUNCS(lio_listio,[
    LINK_WRAPPERS+=",-wrap,lio_listio"
], [])
LIBS=$OLD_LIBS

# memory-mapped files
LINK_WRAPPERS+=",-wrap,mmap"
LINK_WRAPPERS+=",-wrap,mmap64"
LINK_WRAPPERS+=",-wrap,msync"
LINK_WRAPPERS+=",-wrap,munmap"

# status functions
LINK_WRAPPERS+=",-wrap,fstat"
LINK_WRAPPERS+=",-wrap,stat"
AC_CHECK_FUNCS(statfs,[
    LINK_WRAPPERS+=",-wrap,statfs"
],[])
AC_CHECK_FUNCS(fstatfs,[
    LINK_WRAPPERS+=",-wrap,fstatfs"
],[])

AC_CHECK_FUNCS(__lxstat,[
    LINK_WRAPPERS+=",-wrap,__lxstat"
],[])
AC_CHECK_FUNCS(__xstat,[
    LINK_WRAPPERS+=",-wrap,__xstat"
],[])
AC_CHECK_FUNCS(__fxstat,[
    LINK_WRAPPERS+=",-wrap,__fxstat"
],[])

AC_CHECK_FUNCS(posix_fadvise, [
    LINK_WRAPPERS+=",-wrap,posix_fadvise"
],[])

# directory functions
LINK_WRAPPERS+=",-wrap,chdir"
LINK_WRAPPERS+=",-wrap,fchdir"
LINK_WRAPPERS+=",-wrap,__getcwd_chk"
LINK_WRAPPERS+=",-wrap,getcwd"
LINK_WRAPPERS+=",-wrap,getwd"
LINK_WRAPPERS+=",-wrap,get_current_dir_name"
LINK_WRAPPERS+=",-wrap,mkdir"
LINK_WRAPPERS+=",-wrap,rmdir"

# FILE* functions
LINK_WRAPPERS+=",-wrap,fclose"
LINK_WRAPPERS+=",-wrap,fflush"
LINK_WRAPPERS+=",-wrap,fopen"
LINK_WRAPPERS+=",-wrap,freopen"
LINK_WRAPPERS+=",-wrap,setbuf"
LINK_WRAPPERS+=",-wrap,setvbuf"

LINK_WRAPPERS+=",-wrap,fprintf"
LINK_WRAPPERS+=",-wrap,fscanf"
LINK_WRAPPERS+=",-wrap,vfprintf"
LINK_WRAPPERS+=",-wrap,vfscanf"

LINK_WRAPPERS+=",-wrap,fgetc"
LINK_WRAPPERS+=",-wrap,fgets"
LINK_WRAPPERS+=",-wrap,fputc"
LINK_WRAPPERS+=",-wrap,fputs"
LINK_WRAPPERS+=",-wrap,getc"
LINK_WRAPPERS+=",-wrap,putc"
LINK_WRAPPERS+=",-wrap,ungetc"

LINK_WRAPPERS+=",-wrap,fread"
LINK_WRAPPERS+=",-wrap,fwrite"

LINK_WRAPPERS+=",-wrap,fgetpos"
LINK_WRAPPERS+=",-wrap,fseek"
LINK_WRAPPERS+=",-wrap,fsetpos"
LINK_WRAPPERS+=",-wrap,ftell"
LINK_WRAPPERS+=",-wrap,rewind"

LINK_WRAPPERS+=",-wrap,clearerr"
LINK_WRAPPERS+=",-wrap,feof"
LINK_WRAPPERS+=",-wrap,ferror"

LINK_WRAPPERS+=",-wrap,fseeko"
LINK_WRAPPERS+=",-wrap,ftello"
LINK_WRAPPERS+=",-wrap,fileno"

# wide character FILE* functions
LINK_WRAPPERS+=",-wrap,fwprintf"
LINK_WRAPPERS+=",-wrap,fwscanf"
LINK_WRAPPERS+=",-wrap,vfwprintf"
LINK_WRAPPERS+=",-wrap,vfwscanf"
LINK_WRAPPERS+=",-wrap,fgetwc"
LINK_WRAPPERS+=",-wrap,fgetws"
LINK_WRAPPERS+=",-wrap,fputwc"
LINK_WRAPPERS+=",-wrap,fputws"
LINK_WRAPPERS+=",-wrap,fwide"
LINK_WRAPPERS+=",-wrap,getwc"
LINK_WRAPPERS+=",-wrap,putwc"
LINK_WRAPPERS+=",-wrap,ungetwc"

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
AC_SUBST(LINK_WRAPPERS)

AC_CONFIG_FILES([Makefile
                 meta/Makefile
                 meta/src/Makefile
                 server/Makefile
                 server/src/Makefile
                 client/Makefile
                 client/src/Makefile
                 client/unifyfs.pc
                 client/unifyfs-api.pc
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
                 util/unifyfs-api-client/Makefile
                 util/unifyfs-api-client/src/Makefile
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

    Supported I/O wrappers:

    ${LINK_WRAPPERS}

])
