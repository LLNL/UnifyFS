AC_DEFUN([UNIFYFS_AC_SPATH], [
  AC_CHECK_LIB([spath], [spath_strdup_reduce_str],
    [
      LIBS="$LIBS -lspath"
      AC_DEFINE([HAVE_SPATH], [1], [Defined if you have spath])
      AM_CONDITIONAL([HAVE_AM_SPATH], [true])
    ],[
      AC_MSG_WARN([couldn't find a suitable libspath])
      AM_CONDITIONAL([HAVE_AM_SPATH], [false])
    ],
    []
  )
])
