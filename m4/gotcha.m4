AC_DEFUN([UNIFYFS_AC_GOTCHA], [
  # preserve state of flags
  GOTCHA_OLD_CFLAGS=$CFLAGS
  GOTCHA_OLD_CXXFLAGS=$CXXFLAGS
  GOTCHA_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([gotcha], [AC_HELP_STRING([--with-gotcha=PATH],
    [path to installed libgotcha [default=/usr/local]])], [
    GOTCHA_CFLAGS="-I${withval}/include"
    GOTCHA_LDFLAGS="-L${withval}/lib64"
    CFLAGS="$CFLAGS ${GOTCHA_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${GOTCHA_CFLAGS}"
    LDFLAGS="$LDFLAGS ${GOTCHA_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([gotcha], [gotcha_wrap],
    [
      AC_SUBST(GOTCHA_CFLAGS)
      AC_SUBST(GOTCHA_LDFLAGS)
      AM_CONDITIONAL([HAVE_GOTCHA], [true])
    ],[
      AC_MSG_WARN([couldn't find a suitable libgotcha, use --with-gotcha=PATH])
      AM_CONDITIONAL([HAVE_GOTCHA], [false])
    ],
    []
  )

  # restore flags
  CFLAGS=$GOTCHA_OLD_CFLAGS
  CXXFLAGS=$GOTCHA_OLD_CXXFLAGS
  LDFLAGS=$GOTCHA_OLD_LDFLAGS
])
