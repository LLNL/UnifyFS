AC_DEFUN([UNIFYFS_AC_SPATH], [
  # preserve state of flags
  SPATH_OLD_CFLAGS=$CFLAGS
  SPATH_OLD_CXXFLAGS=$CXXFLAGS
  SPATH_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([spath], [AC_HELP_STRING([--with-spath=PATH],
    [path to installed libspath [default=/usr/local]])], [
    SPATH_CFLAGS="-I${withval}/include"
    SPATH_LDFLAGS="-L${withval}/lib64 -L${withval}/lib"
    SPATH_LIBS="-lspath"
    CFLAGS="$CFLAGS ${SPATH_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${SPATH_CFLAGS}"
    LDFLAGS="$LDFLAGS ${SPATH_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([spath], [spath_strdup_reduce_str],
    [
      AC_SUBST(SPATH_CFLAGS)
      AC_SUBST(SPATH_LDFLAGS)
      AC_SUBST(SPATH_LIBS)
      AC_DEFINE([HAVE_SPATH], [1], [Defined if you have spath])
      AM_CONDITIONAL([HAVE_AM_SPATH], [true])
    ],[
      AC_MSG_WARN([couldn't find a suitable libspath, use --with-spath=PATH])
      AM_CONDITIONAL([HAVE_AM_SPATH], [false])
    ],
    []
  )

  # restore flags
  CFLAGS=$SPATH_OLD_CFLAGS
  CXXFLAGS=$SPATH_OLD_CXXFLAGS
  LDFLAGS=$SPATH_OLD_LDFLAGS
])
