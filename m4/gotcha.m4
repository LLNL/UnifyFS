AC_DEFUN([UNIFYCR_AC_GOTCHA], [
  AC_ARG_WITH([gotcha], [AC_HELP_STRING([--with-gotcha=PATH],
    [path to installed libgotcha [default=/usr/local]])], [
    GOTCHA_CFLAGS="-I${withval}/include"
    GOTCHA_LDFLAGS="-L${withval}/lib64"
    CFLAGS="$CFLAGS ${GOTCHA_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${GOTCHA_CFLAGS}"
    LDFLAGS="$LDFLAGS ${GOTCHA_LDFLAGS}"
  ], [])

  AC_SEARCH_LIBS([gotcha_wrap], [gotcha],
    [AC_SUBST(GOTCHA_CFLAGS)
     AC_SUBST(GOTCHA_LDFLAGS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libgotcha, use --with-gotcha=PATH])],
    []
  )
])
