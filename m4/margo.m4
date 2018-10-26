AC_DEFUN([UNIFYCR_AC_MARGO], [
  # preserve state of flags
  MARGO_OLD_CFLAGS=$CFLAGS
  MARGO_OLD_CXXFLAGS=$CXXFLAGS
  MARGO_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([margo], [AC_HELP_STRING([--with-margo=PATH],
    [path to installed libmargo [default=/usr/local]])], [
    MARGO_CFLAGS="-I${withval}/include"
    MARGO_LDFLAGS="-L${withval}/lib"
    MARGO_LIBS="-lmargo"
    CFLAGS="$CFLAGS ${MARGO_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${MARGO_CFLAGS}"
    LDFLAGS="$LDFLAGS ${MARGO_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([margo], [margo_init],
    [AC_SUBST(MARGO_CFLAGS)
     AC_SUBST(MARGO_LDFLAGS)
     AC_SUBST(MARGO_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libmargo, use --with-margo=PATH])],
    []
  )

  # restore flags
  CFLAGS=$MARGO_OLD_CFLAGS
  CXXFLAGS=$MARGO_OLD_CXXFLAGS
  LDFLAGS=$MARGO_OLD_LDFLAGS
])
