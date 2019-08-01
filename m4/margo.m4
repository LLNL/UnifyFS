AC_DEFUN([UNIFYFS_AC_MARGO], [
  # preserve state of flags
  MARGO_OLD_CFLAGS=$CFLAGS
  MARGO_OLD_CXXFLAGS=$CXXFLAGS
  MARGO_OLD_LDFLAGS=$LDFLAGS

  PKG_CHECK_MODULES([MARGO],[margo],
   [
    AC_SUBST(MARGO_CFLAGS)
    AC_SUBST(MARGO_LIBS)
   ],
   [AC_MSG_ERROR(m4_normalize([
     couldn't find a suitable libmargo, set environment variable
     PKG_CONFIG_PATH=paths/for/{mercury,argobots,margo}/lib/pkgconfig
   ]))])

  # restore flags
  CFLAGS=$MARGO_OLD_CFLAGS
  CXXFLAGS=$MARGO_OLD_CXXFLAGS
  LDFLAGS=$MARGO_OLD_LDFLAGS
])
