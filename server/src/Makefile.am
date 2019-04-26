bin_PROGRAMS = unifycrd

unifycrd_SOURCES = \
  arraylist.c \
  arraylist.h \
  margo_server.c \
  margo_server.h \
  unifycr_cmd_handler.c \
  unifycr_global.h \
  unifycr_init.c \
  unifycr_init.h \
  unifycr_metadata.c \
  unifycr_metadata.h \
  unifycr_request_manager.c \
  unifycr_request_manager.h \
  unifycr_service_manager.c \
  unifycr_service_manager.h \
  unifycr_setup.h \
  unifycr_sock.c \
  unifycr_sock.h

unifycrd_LDFLAGS = -static \
  $(LEVELDB_LDFLAGS) \
  $(MARGO_LDFLAGS) \
  $(FLATCC_LDFLAGS)

unifycrd_LDADD = \
  $(top_builddir)/common/src/libunifycr_common.la \
  $(top_builddir)/meta/src/libmdhim.a \
  $(LEVELDB_LIBS) \
  $(MPI_CLDFLAGS) \
  $(MARGO_LIBS) \
  $(FLATCC_LIBS) \
  -lpthread -lm -lstdc++ -lrt

AM_CPPFLAGS = \
  -I$(top_srcdir)/common/src \
  -I$(top_srcdir)/client/src \
  -I$(top_srcdir)/meta/src \
  -I$(top_srcdir)/meta/src/uthash \
  -I$(top_srcdir)/meta/src/Mlog2

AM_CFLAGS = -Wall \
  $(LEVELDB_CFLAGS) \
  $(MPI_CFLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS) \
  $(MARGO_CFLAGS) \
  $(FLATCC_CFLAGS)

CLEANFILES = $(bin_PROGRAMS)
