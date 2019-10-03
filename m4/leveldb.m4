AC_DEFUN([UNIFYFS_AC_LEVELDB], [
  # preserve state of flags
  LEVELDB_OLD_CFLAGS=$CFLAGS
  LEVELDB_OLD_LDFLAGS=$LDFLAGS

  AC_ARG_WITH([leveldb], [AC_HELP_STRING([--with-leveldb=PATH],
    [path to installed libleveldb [default=/usr/local]])], [
    LEVELDB_CFLAGS="-I${withval}/include"
    LEVELDB_LDFLAGS="-L${withval}/lib -L${withval}/lib64"
    CFLAGS="$CFLAGS ${LEVELDB_CFLAGS}"
    LDFLAGS="$LDFLAGS ${LEVELDB_LDFLAGS}"
  ], [])

  AC_CHECK_LIB([leveldb], [leveldb_open],
    [LEVELDB_LIBS="-lleveldb"
     AC_SUBST(LEVELDB_CFLAGS)
     AC_SUBST(LEVELDB_LDFLAGS)
     AC_SUBST(LEVELDB_LIBS)
    ],
    [AC_MSG_ERROR([couldn't find a suitable libleveldb, use --with-leveldb=PATH])],
    []
  )

  # restore flags
  CFLAGS=$LEVELDB_OLD_CFLAGS
  LDFLAGS=$LEVELDB_OLD_LDFLAGS
])
