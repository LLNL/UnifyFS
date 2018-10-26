AC_DEFUN([UNIFYCR_AC_MERCURY], [
  # preserve state of flags
  MERCURY_OLD_CFLAGS=$CFLAGS
  MERCURY_OLD_CXXFLAGS=$CXXFLAGS
  MERCURY_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([mercury], [AC_HELP_STRING([--with-mercury=PATH],
    [path to installed libmercury [default=/usr/local]])], [
    MERCURY_CFLAGS="-I${withval}/include"
    MERCURY_LDFLAGS="-L${withval}/lib"
    MERCURY_LIBS="-lmercury"
    CFLAGS="$CFLAGS ${MERCURY_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${MERCURY_CFLAGS}"
    LDFLAGS="$LDFLAGS ${MERCURY_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([mercury], [HG_Init],
    [AC_SUBST(MERCURY_CFLAGS)
     AC_SUBST(MERCURY_LDFLAGS)
     AC_SUBST(MERCURY_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libmercury, use --with-mercury=PATH])],
    []
  )

  # restore flags
  CFLAGS=$MERCURY_OLD_CFLAGS
  CXXFLAGS=$MERCURY_OLD_CXXFLAGS
  LDFLAGS=$MERCURY_OLD_LDFLAGS
])
