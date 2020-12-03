bin_PROGRAMS = unifyfs

unifyfs_SOURCES = unifyfs.c \
                  unifyfs-rm.c

noinst_HEADERS = unifyfs.h

unifyfs_LDADD = $(top_builddir)/common/src/libunifyfs_common.la

AM_CPPFLAGS = -I$(top_srcdir)/common/src \
              -DBINDIR=\"$(bindir)\" \
              -DSBINDIR=\"$(sbindir)\" \
              -DLIBEXECDIR=\"$(libexecdir)\"

AM_CFLAGS = -Wall -Werror

CLEANFILES = $(bin_PROGRAMS)

