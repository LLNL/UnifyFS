AC_DEFUN([UNIFYFS_AC_SPATH], [
  # preserve state of flags
  SPATH_OLD_CFLAGS=$CFLAGS
  SPATH_OLD_CXXFLAGS=$CXXFLAGS
  SPATH_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([spath], [AC_HELP_STRING([--with-spath=PATH],
    [path to installed libspath [default=/usr/local]])],
    [
      SPATH_DIR="${withval}"
      SPATH_CFLAGS="-I${SPATH_DIR}/include"
      SPATH_LDFLAGS="-L${SPATH_DIR}/lib64 -L${SPATH_DIR}/lib -Wl,-rpath,${SPATH_DIR}/lib64 -Wl,-rpath,${SPATH_DIR}/lib"
      CFLAGS="$CFLAGS ${SPATH_CFLAGS}"
      CXXFLAGS="$CXXFLAGS ${SPATH_CFLAGS}"
      LDFLAGS="$LDFLAGS ${SPATH_LDFLAGS}"
    ],
    [
      SPATH_CFLAGS=""
      SPATH_LDFLAGS=""
    ]
  )

  AC_CHECK_LIB([spath], [spath_strdup_reduce_str],
    [
      SPATH_LIBS="${SPATH_LDFLAGS} -lspath"
      AC_SUBST(SPATH_CFLAGS)
      AC_SUBST(SPATH_LDFLAGS)
      AC_SUBST(SPATH_LIBS)
      AM_CONDITIONAL([HAVE_SPATH], [true])
      AC_DEFINE([USE_SPATH], [1], [Defined if you have libspath])
    ],
    [
      AC_MSG_WARN([couldn't find a suitable libspath, use --with-spath=PATH])
      AM_CONDITIONAL([HAVE_SPATH], [false])
    ],
    []
  )

  # restore flags
  CFLAGS=$SPATH_OLD_CFLAGS
  CXXFLAGS=$SPATH_OLD_CXXFLAGS
  LDFLAGS=$SPATH_OLD_LDFLAGS
])
