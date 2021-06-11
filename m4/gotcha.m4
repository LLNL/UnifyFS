AC_DEFUN([UNIFYFS_AC_GOTCHA], [
  # preserve state of flags
  GOTCHA_OLD_CFLAGS=$CFLAGS
  GOTCHA_OLD_CXXFLAGS=$CXXFLAGS
  GOTCHA_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([gotcha], [AC_HELP_STRING([--with-gotcha=PATH],
    [path to installed libgotcha [default=/usr/local]])],
    [
      GOTCHA_DIR="${withval}"
      GOTCHA_CFLAGS="-I${GOTCHA_DIR}/include"
      GOTCHA_LDFLAGS="-L${GOTCHA_DIR}/lib64 -L${GOTCHA_DIR}/lib -Wl,-rpath,${GOTCHA_DIR}/lib64 -Wl,-rpath,${GOTCHA_DIR}/lib"
      CFLAGS="$CFLAGS ${GOTCHA_CFLAGS}"
      CXXFLAGS="$CXXFLAGS ${GOTCHA_CFLAGS}"
      LDFLAGS="$LDFLAGS ${GOTCHA_LDFLAGS}"
    ],
    [
      GOTCHA_CFLAGS=""
      GOTCHA_LDFLAGS=""
    ]
  )

  AC_CHECK_LIB([gotcha], [gotcha_wrap],
    [
      GOTCHA_LIBS="${GOTCHA_LDFLAGS} -lgotcha"
      AC_SUBST(GOTCHA_CFLAGS)
      AC_SUBST(GOTCHA_LDFLAGS)
      AC_SUBST(GOTCHA_LIBS)
      AM_CONDITIONAL([HAVE_GOTCHA], [true])
    ],
    [
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
