lib_LTLIBRARIES = libunifyfs.la libunifyfs_api.la
libunifyfsdir = $(includedir)

if HAVE_GOTCHA
lib_LTLIBRARIES += libunifyfs_gotcha.la
libunifyfs_gotchadir = $(includedir)
endif

if HAVE_FORTRAN
lib_LTLIBRARIES += libunifyfsf.la
endif

AM_CFLAGS = -Wall -Wno-strict-aliasing -Werror

include_HEADERS = unifyfs.h
pkginclude_HEADERS = unifyfs_api.h

if HAVE_FORTRAN
include_HEADERS += unifyfsf.h
endif

CLIENT_COMMON_CPPFLAGS = \
  -Wall -Werror \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/client/include \
  -I$(top_srcdir)/common/src

CLIENT_COMMON_CFLAGS = \
  -Wall -Werror \
  $(MPI_CFLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS) \
  $(MARGO_CFLAGS)

CLIENT_COMMON_LDFLAGS = \
  -version-info $(LIBUNIFYFS_LT_VERSION) \
  $(MPI_CLDFLAGS) \
  $(MARGO_LDFLAGS)

CLIENT_COMMON_LIBADD = \
  $(top_builddir)/common/src/libunifyfs_common.la \
  $(MARGO_LIBS) \
  -lrt -lpthread

CLIENT_COMMON_SOURCES = \
  margo_client.c \
  margo_client.h \
  unifyfs.c \
  unifyfs.h \
  unifyfs-dirops.h \
  unifyfs-dirops.c \
  unifyfs-fixed.c \
  unifyfs-fixed.h \
  unifyfs-internal.h \
  unifyfs-stdio.c \
  unifyfs-stdio.h \
  unifyfs-sysio.c \
  unifyfs-sysio.h \
  uthash.h \
  utlist.h

if USE_PMPI_WRAPPERS
CLIENT_COMMON_SOURCES += \
  pmpi_wrappers.c \
  pmpi_wrappers.h
endif

CLIENT_API_SOURCES = \
  unifyfs_api.h \
  unifyfs_api_internal.h \
  unifyfs_api.c \
  unifyfs_api_file.c \
  unifyfs_api_io.c \
  unifyfs_api_transfer.c

libunifyfs_api_la_SOURCES  = $(CLIENT_API_SOURCES)
libunifyfs_api_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_api_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_api_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_api_la_LIBADD   = $(CLIENT_COMMON_LIBADD)

libunifyfs_la_SOURCES  = $(CLIENT_COMMON_SOURCES)
libunifyfs_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_la_LIBADD   = $(CLIENT_COMMON_LIBADD)

if HAVE_GOTCHA

libunifyfs_gotcha_la_SOURCES  = $(CLIENT_COMMON_SOURCES) gotcha_map_unifyfs_list.c
libunifyfs_gotcha_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS) -DUNIFYFS_GOTCHA
libunifyfs_gotcha_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS) $(GOTCHA_CFLAGS)
libunifyfs_gotcha_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS) $(GOTCHA_LDFLAGS)
libunifyfs_gotcha_la_LIBADD   = $(CLIENT_COMMON_LIBADD) -lgotcha

endif

if HAVE_FORTRAN

libunifyfsf_la_SOURCES  = unifyfsf.c
libunifyfsf_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfsf_la_CFLAGS   = $(AM_CFLAGS) $(CLIENT_COMMON_CFLAGS)
libunifyfsf_la_LIBADD   = libunifyfs_gotcha.la

endif
