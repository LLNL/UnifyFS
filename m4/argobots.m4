AC_DEFUN([UNIFYCR_AC_ARGOBOTS], [
  # preserve state of flags
  ARGOBOTS_OLD_CFLAGS=$CFLAGS
  ARGOBOTS_OLD_CXXFLAGS=$CXXFLAGS
  ARGOBOTS_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([argobots], [AC_HELP_STRING([--with-argobots=PATH],
    [path to installed libabt [default=/usr/local]])], [
    ARGOBOTS_CFLAGS="-I${withval}/include"
    ARGOBOTS_LDFLAGS="-L${withval}/lib"
    CFLAGS="$CFLAGS ${ARGOBOTS_CFLAGS}"
    CXXFLAGS="$CXXFLAGS ${ARGOBOTS_CFLAGS}"
    LDFLAGS="$LDFLAGS ${ARGOBOTS_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([abt], [ABT_init],
    [ARGOBOTS_LIBS="-labt"
     AC_SUBST(ARGOBOTS_CFLAGS)
     AC_SUBST(ARGOBOTS_LDFLAGS)
     AC_SUBST(ARGOBOTS_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libabt, use --with-argobots=PATH])],
    []
  )

  # restore flags
  CFLAGS=$ARGOBOTS_OLD_CFLAGS
  CXXFLAGS=$ARGOBOTS_OLD_CXXFLAGS
  LDFLAGS=$ARGOBOTS_OLD_LDFLAGS
])
