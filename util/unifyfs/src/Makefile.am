include $(top_srcdir)/common/src/Makefile.mk

bin_PROGRAMS = unifyfs

unifyfs_SOURCES = \
  $(UNIFYFS_COMMON_SRCS) \
  unifyfs.c \
  unifyfs-rm.c

noinst_HEADERS = unifyfs.h

unifyfs_LDADD = $(UNIFYFS_COMMON_LIBS)

AM_CPPFLAGS = -I$(top_srcdir)/common/src \
              -DBINDIR=\"$(bindir)\" \
              -DSBINDIR=\"$(sbindir)\" \
              -DLIBEXECDIR=\"$(libexecdir)\"

AM_CFLAGS = -Wall -Werror $(UNIFYFS_COMMON_FLAGS)

CLEANFILES = $(bin_PROGRAMS)

