dnl @synopsis CHECK_ARCH()
dnl
dnl This macro checks for the architectur (BGQ/Linux) and defines the
dnl appropriate CFLAGS in the code. BG/Q builds also disable NUMA support

AC_DEFUN([CHECK_ARCH],

#
# Handle user hints
#
[AC_MSG_CHECKING(architecture type)
AC_ARG_WITH([arch],
[AS_HELP_STRING([--with-numa=ARCH],[specify the architecture as bgq or linux])],
[if test "$withval" != no ; then
  AC_MSG_RESULT(yes)
    ARCH=$withval
    if test "${ARCH}" = "bgq"
    then
        AC_DEFINE([MACHINE_BGQ], [1], [Define if architecture is BG/Q])
    fi
else
    AC_MSG_RESULT(no)
fi])

])
