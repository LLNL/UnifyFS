bin_PROGRAMS = unifycr

unifycr_SOURCES = unifycr.c \
                  unifycr-rm.c

noinst_HEADERS = unifycr.h

unifycr_LDADD = $(top_builddir)/common/src/libunifycr_common.la

AM_CPPFLAGS = -I$(top_srcdir)/common/src \
              -DBINDIR=\"$(bindir)\" \
              -DSBINDIR=\"$(sbindir)\"

AM_CFLAGS = -Wall

CLEANFILES = $(bin_PROGRAMS)

