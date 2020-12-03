AC_DEFUN([UNIFYFS_AC_LEVELDB], [
  # preserve state of flags
  LEVELDB_OLD_CFLAGS=$CFLAGS
  LEVELDB_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_VAR([LEVELDB_ROOT], [Set the path to the LevelDB installation Directory])

  AS_IF([test -n "$LEVELDB_ROOT"],[
    AC_MSG_NOTICE([LEVELDB_ROOT is set, checking for LevelDB in $LEVELDB_ROOT])
    LEVELDB_CFLAGS="-I${LEVELDB_ROOT}/include"
    LEVELDB_LDFLAGS="-L${LEVELDB_ROOT}/lib -L${LEVELDB_ROOT}/lib64"
    CFLAGS="$CFLAGS ${LEVELDB_CFLAGS}"
    LDFLAGS="$LDFLAGS ${LEVELDB_LDFLAGS}"
  ],[])

  AC_CHECK_LIB([leveldb], [leveldb_open],
    [LEVELDB_LIBS="-lleveldb"
     AC_SUBST(LEVELDB_CFLAGS)
     AC_SUBST(LEVELDB_LDFLAGS)
     AC_SUBST(LEVELDB_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libleveldb, use LEVELDB_ROOT to set the path to the installation directory.])],
    []
  )

  # restore flags
  CFLAGS=$LEVELDB_OLD_CFLAGS
  LDFLAGS=$LEVELDB_OLD_LDFLAGS
])
