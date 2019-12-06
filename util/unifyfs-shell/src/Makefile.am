bin_PROGRAMS = unifyfs-shell-gotcha unifyfs-shell-static

unifyfs_shell_gotcha_SOURCES = unifyfs-shell.c \
                               unifyfs-shell-util.c \
                               unifyfs-shell-main.c \
                               unifyfs-shell-status.c \
                               unifyfs-shell-ls.c \
                               unifyfs-shell-mkdir.c \
                               unifyfs-shell-cd.c \
                               unifyfs-shell-pwd.c \
                               unifyfs-shell-stat.c

unifyfs_shell_static_SOURCES = unifyfs-shell.c \
                               unifyfs-shell-util.c \
                               unifyfs-shell-main.c \
                               unifyfs-shell-status.c \
                               unifyfs-shell-ls.c \
                               unifyfs-shell-mkdir.c \
                               unifyfs-shell-cd.c \
                               unifyfs-shell-pwd.c \
                               unifyfs-shell-stat.c

noinst_HEADERS = unifyfs-shell.h

AM_CPPFLAGS = $(MPI_CFLAGS) \
              -I$(top_srcdir)/common/src \
              -I$(top_srcdir)/client/src

AM_CFLAGS = -Wall

unifyfs_shell_gotcha_LDADD = $(top_builddir)/common/src/libunifyfs_common.la \
                      $(top_builddir)/client/src/libunifyfs_gotcha.la -lrt -lm

unifyfs_shell_gotcha_LDFLAGS = $(MPI_CLDFLAGS)

unifyfs_shell_static_LDADD = $(top_builddir)/common/src/libunifyfs_common.la \
                             $(top_builddir)/client/src/libunifyfs.la -lrt -lm

unifyfs_shell_static_LDFLAGS = -static $(CP_WRAPPERS) $(MPI_CLDFLAGS)

CLEANFILES = $(bin_PROGRAMS)

