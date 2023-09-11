AC_DEFUN([UNIFYFS_AC_OPENSSL], [
  # preserve state of flags
  OPENSSL_OLD_CFLAGS=$CFLAGS
  OPENSSL_OLD_CXXFLAGS=$CXXFLAGS
  OPENSSL_OLD_LDFLAGS=$LDFLAGS

  PKG_CHECK_MODULES([OPENSSL],[openssl],
   [
    AC_SUBST(OPENSSL_CFLAGS)
    AC_SUBST(OPENSSL_LIBS)
   ],
   [AC_MSG_ERROR(m4_normalize([
     couldn't find a suitable openssl-devel
   ]))])




  AC_CHECK_LIB([crypto], [EVP_Digest],
    [
      AM_CONDITIONAL([HAVE_OPENSSL_EVP], [true])
    ],
    [
      AC_MSG_ERROR([couldn't find a sufficiently new OpenSSL installation])
    ],
    []
  )


  # restore flags
  CFLAGS=$OPENSSL_OLD_CFLAGS
  CXXFLAGS=$OPENSSL_OLD_CXXFLAGS
  LDFLAGS=$OPENSSL_OLD_LDFLAGS
])
