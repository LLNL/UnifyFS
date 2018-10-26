AC_DEFUN([UNIFYCR_AC_FLATBUFFERS], [
  # preserve state of flags
  FLATBUFFERS_OLD_CFLAGS=$CFLAGS
  FLATBUFFERS_OLD_CXXFLAGS=$CXXFLAGS
  FLATBUFFERS_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([flatbuffers], [AC_HELP_STRING([--with-flatbuffers=PATH],
    [path to installed libflatcc_d [default=/usr/local]])], [
    FLATBUFFERS_CFLAGS="-I${withval}/include"
    FLATBUFFERS_LDFLAGS="-L${withval}/lib"
    FLATBUFFERS_LIBS="-lflatcc_d"
    CFLAGS="$CFLAGS ${FLATBUFFERS_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${FLATBUFFERS_CFLAGS}"
    LDFLAGS="$LDFLAGS ${FLATBUFFERS_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([flatcc_d], [flatcc_create_context],
    [AC_SUBST(FLATBUFFERS_CFLAGS)
     AC_SUBST(FLATBUFFERS_LDFLAGS)
     AC_SUBST(FLATBUFFERS_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libflatcc_d, use --with-flatbuffers=PATH])],
    []
  )

  # restore flags
  CFLAGS=$FLATBUFFERS_OLD_CFLAGS
  CXXFLAGS=$FLATBUFFERS_OLD_CXXFLAGS
  LDFLAGS=$FLATBUFFERS_OLD_LDFLAGS
])
