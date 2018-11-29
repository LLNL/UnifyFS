AC_DEFUN([UNIFYCR_AC_RANKSTR], [
  # preserve state of flags
  RANKSTR_OLD_CFLAGS=$CFLAGS
  RANKSTR_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([rankstr], [AC_HELP_STRING([--with-rankstr=PATH],
    [path to installed librankstr [default=/usr/local]])], [
    RANKSTR_CFLAGS="-I${withval}/include"
    RANKSTR_LDFLAGS="-L${withval}/lib64"
    CFLAGS="$CFLAGS ${RANKSTR_CFLAGS}"
    LDFLAGS="$LDFLAGS ${RANKSTR_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([rankstr], [rankstr_mpi],
    [RANKSTR_LIBS="-lrankstr"
     AC_SUBST(RANKSTR_CFLAGS)
     AC_SUBST(RANKSTR_LDFLAGS)
     AC_SUBST(RANKSTR_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable librankstr, use --with-rankstr=PATH])],
    []
  )

  # restore flags
  CFLAGS=$RANKSTR_OLD_CFLAGS
  LDFLAGS=$RANKSTR_OLD_LDFLAGS
])
