AC_DEFUN([UNIFYFS_AC_SPATH], [

  AC_ARG_WITH([spath], [AS_HELP_STRING([--with-spath=PATH],
    [path to installed libspath [default=/usr/local]])],
    [],
    [with_spath=no]
  )

  AS_IF([test x$with_spath != xno],
    [
      CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
      LDFLAGS="-L${withval}/lib -L${withval}/lib64 ${LDFLAGS}"
    ]
  )

  AC_CHECK_LIB([spath], [spath_strdup_reduce_str],
    [
      LIBS="$LIBS -lspath"
      AC_DEFINE([HAVE_SPATH], [1], [Defined if you have spath])
    ],[
      AC_MSG_WARN([couldn't find a suitable libspath])
    ],
    []
  )
])
