lib_LTLIBRARIES = libunifycr.la libunifycr_gotcha.la

libunifycrdir = $(includedir)
libunifycr_gotchadir = $(includedir)

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
  unifycr-defs.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  uthash.h \
  utlist.h

libunifycr_la_CPPFLAGS = -I$(top_builddir)/client
libunifycr_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION) -lcrypto -lrt -lpthread

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
  unifycr-defs.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  uthash.h \
  utlist.h

libunifycr_gotcha_la_CPPFLAGS = -DUNIFYCR_GOTCHA -I$(top_builddir)/client $(GOTCHA_CFLAGS)
libunifycr_gotcha_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION) $(GOTCHA_LDFLAGS) -lgotcha -lcrypto -lrt -lpthread

AM_CFLAGS = -Wall -Wno-strict-aliasing
