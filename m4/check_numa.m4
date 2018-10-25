dnl @synopsis CHECK_NUMA()
dnl
dnl This macro searches for an installed numa library. If nothing was
dnl specified when calling configure, it searches first in /usr/local
dnl and then in /usr. If the --with-numa=DIR is specified, it will try
dnl to find it in DIR/include/numa.h and DIR/lib/libnuma.a. If
dnl --without-numa is specified, the library is not searched at all.
dnl
dnl If either the header file (numa.h) or the library (libnuma) is not
dnl found, the configuration exits on error, asking for a valid numa
dnl installation directory or --without-numa.
dnl
dnl The macro defines the symbol HAVE_LIBNUMA if the library is found. You
dnl should use autoheader to include a definition for this symbol in a
dnl config.h file. Sample usage in a C/C++ source is as follows:
dnl
dnl   #ifdef HAVE_LIBNUMA
dnl   #include <numa.h>
dnl   #endif /* HAVE_LIBNUMA */
dnl
dnl @category InstalledPackages
dnl @author Loic Dachary <loic@senga.org>
dnl @version 2004-09-20
dnl @license GPLWithACException

AC_DEFUN([CHECK_NUMA], [

#
# Handle user hints
#
LOOK_FOR_NUMA="no"
AC_MSG_CHECKING(if numa is wanted )
AC_ARG_WITH([numa],
  [AS_HELP_STRING([--with-numa=DIR],[root directory path of libnuma installation (defaults to /usr/local or /usr if not found in /usr/local)])],
  [if test "$withval" != "no" ; then
    AC_MSG_RESULT(yes)
    LOOK_FOR_NUMA="yes"
    if test "$withval" != "yes" ; then
      #
      # given a path to look in
      #
      NUMA_HOME="$withval"
    fi
  else
    AC_MSG_RESULT(no)
  fi],
  [AC_MSG_RESULT(no)]
)

#
# Locate numa, if wanted
#
if test "$LOOK_FOR_NUMA" = "yes" ; then
    #
    # determine where to look for libnuma
    #
    if test -n "${NUMA_HOME}"
    then
        AC_MSG_NOTICE([include libnuma from ${NUMA_HOME}])

        #
        # Look for NUMA where user tells us it is first
        #
        CFLAGS="-I${NUMA_HOME}/include $CFLAGS"
        LDFLAGS="-L${NUMA_HOME}/lib $LDFLAGS"
    else
        AC_MSG_NOTICE([checking for libnuma installation in default locations])
    fi

    #
    # Locate numa
    #
    AC_LANG_SAVE
    AC_LANG_C
    AC_CHECK_HEADER(numa.h, [numa_cv_numa_h=yes], [numa_cv_numa_h=no])
    AC_CHECK_LIB(numa, numa_num_possible_nodes, [numa_cv_libnuma=yes], [numa_cv_libnuma=no])
    AC_LANG_RESTORE

    #
    # Determine whether we found it
    #
    if test "$numa_cv_libnuma" != "yes" -o "$numa_cv_numa_h" != "yes"
    then
            #
            # If either header or library was not found, bomb
            #
            AC_MSG_ERROR(either specify a valid numa installation with --with-numa=DIR or disable numa usage with --without-numa)
    fi
fi

])
