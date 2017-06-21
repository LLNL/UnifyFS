dnl @synopsis CHECK_NUMA()
dnl
dnl This macro searches for an installed numa library. If nothing was
dnl specified when calling configure, it searches first in /usr/local
dnl and then in /usr. If the --with-numa=DIR is specified, it will try
dnl to find it in DIR/include/numa.h and DIR/lib/libz.a. If
dnl --without-numa is specified, the library is not searched at all.
dnl
dnl If either the header file (numa.h) or the library (libz) is not
dnl found, the configuration exits on error, asking for a valid numa
dnl installation directory or --without-numa.
dnl
dnl The macro defines the symbol HAVE_LIBZ if the library is found. You
dnl should use autoheader to include a definition for this symbol in a
dnl config.h file. Sample usage in a C/C++ source is as follows:
dnl
dnl   #ifdef HAVE_LIBZ
dnl   #include <numa.h>
dnl   #endif /* HAVE_LIBZ */
dnl
dnl @category InstalledPackages
dnl @author Loic Dachary <loic@senga.org>
dnl @version 2004-09-20
dnl @license GPLWithACException

AC_DEFUN([CHECK_NUMA],

#
# Handle user hints
#
[AC_MSG_CHECKING(if numa is wanted )
AC_ARG_WITH([numa],
[AS_HELP_STRING([--with-numa=DIR],[root directory path of libnuma installation (defaults to /usr/local or /usr if not found in /usr/local)])],
[if test "$withval" != no ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    NUMA_HOME="$withval"
    AC_DEFINE([ENABLE_NUMA_POLICY], [1], [Define if libnuma is available])
  else
    AC_MSG_CHECKING([for libnuma installation in default locations])
  fi
else
  AC_MSG_RESULT(no)
fi])

#
# Locate numa, if wanted
#
if test -n "${NUMA_HOME}"
then
        NUMA_OLD_LDFLAGS=$LDFLAGS
        NUMA_OLD_CPPFLAGS=$LDFLAGS
        LDFLAGS="$LDFLAGS -L${NUMA_HOME}/lib"
        CPPFLAGS="$CPPFLAGS -I${NUMA_HOME}/include"
        AC_LANG_SAVE
        AC_LANG_C
        AC_CHECK_LIB(numa, numa_num_possible_nodes, [numa_cv_libnuma=yes], [numa_cv_libnuma=no])
        AC_CHECK_HEADER(numa.h, [numa_cv_numa_h=yes], [numa_cv_numa_h=no])
        AC_LANG_RESTORE
        if test "$numa_cv_libnuma" = "yes" -a "$numa_cv_numa_h" = "yes"
        then
                #
                # If both library and header were found, use them
                #
                AC_CHECK_LIB(numa, numa_num_possible_nodes)
                AC_MSG_CHECKING(numa in ${NUMA_HOME})
                AC_MSG_RESULT(ok)
        else
                #
                # If either header or library was not found, revert and bomb
                #
                AC_MSG_CHECKING(numa in ${NUMA_HOME})
                LDFLAGS="$NUMA_OLD_LDFLAGS"
                CPPFLAGS="$NUMA_OLD_CPPFLAGS"
                AC_MSG_RESULT(failed)
                AC_MSG_ERROR(either specify a valid numa installation with --with-numa=DIR or disable numa usage with --without-numa)
        fi
fi

])
