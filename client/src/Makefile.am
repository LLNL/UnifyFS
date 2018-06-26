lib_LTLIBRARIES = libunifycr.la libunifycr_gotcha.la

libunifycrdir = $(includedir)
libunifycr_gotchadir = $(includedir)

AM_CFLAGS = -Wall -Wno-strict-aliasing

AM_CPPFLAGS = -I$(top_srcdir)/common/src \
              -I$(top_srcdir)/client/src \
              -I$(top_builddir)/client

libunifycr_HEADERS = unifycr.h
libunifycr_la_SOURCES = \
  unifycr-fixed.c \
  unifycr-fixed.h \
  unifycr-stack.c \
  unifycr-stack.h \
  unifycr-stdio.c \
  unifycr-stdio.h \
  unifycr-sysio.c \
  unifycr-sysio.h \
  unifycr.c \
  unifycr.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  uthash.h \
  utlist.h

libunifycr_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION)
libunifycr_la_LIBADD = $(top_builddir)/common/src/libunifycr_common.la \
					   -lcrypto -lrt -lpthread

libunifycr_gotcha_HEADERS = unifycr.h
libunifycr_gotcha_la_SOURCES = \
  unifycr-fixed.c \
  unifycr-fixed.h \
  unifycr-stack.c \
  unifycr-stack.h \
  unifycr-stdio.c \
  unifycr-stdio.h \
  unifycr-sysio.c \
  unifycr-sysio.h \
  unifycr.c \
  unifycr.h \
  gotcha_map_unifycr_list.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  uthash.h \
  utlist.h

libunifycr_gotcha_la_CPPFLAGS = $(AM_CPPFLAGS) -DUNIFYCR_GOTCHA  $(GOTCHA_CFLAGS)
libunifycr_gotcha_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION) $(GOTCHA_LDFLAGS)
libunifycr_gotcha_la_LIBADD = $(top_builddir)/common/src/libunifycr_common.la \
                              -lgotcha -lcrypto -lrt -lpthread

