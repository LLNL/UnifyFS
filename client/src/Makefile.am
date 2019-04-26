lib_LTLIBRARIES = libunifycr.la libunifycr_gotcha.la libunifycrf.la

libunifycrdir = $(includedir)
libunifycr_gotchadir = $(includedir)

AM_CFLAGS = -Wall -Wno-strict-aliasing

include_HEADERS = unifycr.h unifycrf.h

CLIENT_COMMON_CPPFLAGS = \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/common/src

CLIENT_COMMON_CFLAGS = \
  $(MPI_CFLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS) \
  $(MARGO_CFLAGS) \
  $(FLATCC_CFLAGS)

CLIENT_COMMON_LDFLAGS = \
  -version-info $(LIBUNIFYCR_LT_VERSION) \
  $(MPI_CLDFLAGS)

CLIENT_COMMON_LIBADD = \
  $(top_builddir)/common/src/libunifycr_common.la \
  -lcrypto -lrt -lpthread

CLIENT_COMMON_SOURCES = \
  margo_client.c \
  margo_client.h \
  unifycr.c \
  unifycr.h \
  unifycr-dirops.h \
  unifycr-dirops.c \
  unifycr-fixed.c \
  unifycr-fixed.h \
  unifycr-internal.h \
  unifycr-stack.c \
  unifycr-stack.h \
  unifycr-stdio.c \
  unifycr-stdio.h \
  unifycr-sysio.c \
  unifycr-sysio.h \
  uthash.h \
  utlist.h

if USE_PMPI_WRAPPERS
CLIENT_COMMON_SOURCES += \
  pmpi_wrappers.c \
  pmpi_wrappers.h
endif

libunifycr_la_SOURCES  = $(CLIENT_COMMON_SOURCES)
libunifycr_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifycr_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifycr_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifycr_la_LIBADD   = $(CLIENT_COMMON_LIBADD)

libunifycr_gotcha_la_SOURCES  = $(CLIENT_COMMON_SOURCES) gotcha_map_unifycr_list.h
libunifycr_gotcha_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS) -DUNIFYCR_GOTCHA
libunifycr_gotcha_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS) $(GOTCHA_CFLAGS)
libunifycr_gotcha_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS) $(GOTCHA_LDFLAGS)
libunifycr_gotcha_la_LIBADD   = $(CLIENT_COMMON_LIBADD) -lgotcha

libunifycrf_la_SOURCES  = unifycrf.c
libunifycrf_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifycrf_la_CFLAGS   = $(AM_CFLAGS) $(CLIENT_COMMON_CFLAGS)
libunifycrf_la_LIBADD   = libunifycr_gotcha.la
