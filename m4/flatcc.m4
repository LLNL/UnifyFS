AC_DEFUN([UNIFYCR_AC_FLATCC], [
  # preserve state of flags
  FLATCC_OLD_CFLAGS=$CFLAGS
  FLATCC_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([flatcc], [AC_HELP_STRING([--with-flatcc=PATH],
    [path to installed libflatcc [default=/usr/local]])], [
    FLATCC_CFLAGS="-I${withval}/include"
    FLATCC_LDFLAGS="-L${withval}/lib"
    CFLAGS="$CFLAGS ${FLATCC_CFLAGS}"
    LDFLAGS="$LDFLAGS ${FLATCC_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([flatcc], [flatcc_create_context],
    [FLATCC_LIBS="-lflatcc"
     AC_SUBST(FLATCC_CFLAGS)
     AC_SUBST(FLATCC_LDFLAGS)
     AC_SUBST(FLATCC_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libflatcc, use --with-flatcc=PATH])],
    []
  )

  # restore flags
  CFLAGS=$FLATCC_OLD_CFLAGS
  LDFLAGS=$FLATCC_OLD_LDFLAGS
])
