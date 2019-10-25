AC_DEFUN([UNIFYFS_AC_FLATCC], [
  # preserve state of flags
  FLATCC_OLD_CFLAGS=$CFLAGS
  FLATCC_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_VAR([FLATCC_ROOT],[Set the path to the LevelDB installation directory])

  AS_IF([test -n "$FLATCC_ROOT"],[
    AC_MSG_NOTICE([FLATCC_ROOT is set, checking for flatcc in $FLATCC_ROOT])
    FLATCC_CFLAGS="-I${FLATCC_ROOT}/include"
    FLATCC_LDFLAGS="-L${FLATCC_ROOT}/lib"
    CFLAGS="$CFLAGS ${FLATCC_CFLAGS}"
    LDFLAGS="$LDFLAGS ${FLATCC_LDFLAGS}"
  ],[])

  AC_CHECK_LIB([flatcc], [flatcc_create_context],
    [FLATCC_LIBS="-lflatcc"
     AC_SUBST(FLATCC_CFLAGS)
     AC_SUBST(FLATCC_LDFLAGS)
     AC_SUBST(FLATCC_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libflatcc, use FLATCC_ROOT to set the path to the flatcc installation directory.])],
    []
  )

  # restore flags
  CFLAGS=$FLATCC_OLD_CFLAGS
  LDFLAGS=$FLATCC_OLD_LDFLAGS
])
