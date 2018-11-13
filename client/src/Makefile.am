lib_LTLIBRARIES = libunifycr.la libunifycr_gotcha.la

libunifycrdir = $(includedir)
libunifycr_gotchadir = $(includedir)

AM_CFLAGS = -Wall -Wno-strict-aliasing

include_HEADERS = unifycr.h

libunifycr_la_SOURCES = \
  unifycr.h \
  unifycr-fixed.c \
  unifycr-fixed.h \
  unifycr-stack.c \
  unifycr-stack.h \
  unifycr-stdio.c \
  unifycr-stdio.h \
  unifycr-sysio.c \
  unifycr-sysio.h \
  unifycr-dirops.h \
  unifycr-dirops.c \
  unifycr.c \
  unifycr.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  unifycr_clientcalls_rpc.h \
  unifycr_client.c \
  unifycr_client.h \
  flatbuffers_common_builder.h \
  flatbuffers_common_reader.h \
  ucr_read_builder.h \
  ucr_read_reader.h \
  uthash.h \
  utlist.h

libunifycr_la_CPPFLAGS = \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/common/src

libunifycr_la_CFLAGS = $(MPI_CFLAGS) $(MERCURY_CFLAGS) $(ARGOBOTS_CFLAGS) $(MARGO_CFLAGS) $(FLATCC_CFLAGS)
libunifycr_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION) $(MPI_CLDFLAGS) $(MERCURY_LDFLAGS) $(MERCURY_LIBS) $(ARGOBOTS_LDFLAGS) $(ARGOBOTS_LIBS) $(MARGO_LDFLAGS) $(MARGO_LIBS) $(FLATCC_LDFLAGS) $(FLATCC_LIBS)
libunifycr_la_LIBADD = $(top_builddir)/common/src/libunifycr_common.la \
					   -lcrypto -lrt -lpthread

libunifycr_gotcha_la_SOURCES = \
  unifycr.h \
  unifycr-fixed.c \
  unifycr-fixed.h \
  unifycr-stack.c \
  unifycr-stack.h \
  unifycr-stdio.c \
  unifycr-stdio.h \
  unifycr-sysio.c \
  unifycr-sysio.h \
  unifycr-dirops.h \
  unifycr-dirops.c \
  unifycr.c \
  unifycr.h \
  gotcha_map_unifycr_list.h \
  unifycr-internal.h \
  unifycr-wrap.h \
  unifycr_clientcalls_rpc.h \
  unifycr_client.c \
  unifycr_client.h \
  flatbuffers_common_builder.h \
  flatbuffers_common_reader.h \
  ucr_read_builder.h \
  ucr_read_reader.h \
  uthash.h \
  utlist.h

libunifycr_gotcha_la_CPPFLAGS = \
  -DUNIFYCR_GOTCHA \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/common/src

libunifycr_gotcha_la_CFLAGS = $(GOTCHA_CFLAGS) $(MPI_CFLAGS) $(MERCURY_CFLAGS) $(ARGOBOTS_CFLAGS) $(MARGO_CFLAGS) $(FLATCC_CFLAGS)
libunifycr_gotcha_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION) $(GOTCHA_LDFLAGS) $(MPI_CLDFLAGS) $(MERCURY_LDFLAGS) $(MERCURY_LIBS) $(ARGOBOTS_LDFLAGS) $(ARGOBOTS_LIBS) $(MARGO_LDFLAGS) $(MARGO_LIBS) $(FLATCC_LDFLAGS) $(FLATCC_LIBS)
libunifycr_gotcha_la_LIBADD = $(top_builddir)/common/src/libunifycr_common.la \
                              -lgotcha -lcrypto -lrt -lpthread


