include $(top_srcdir)/common/src/Makefile.mk

lib_LTLIBRARIES = libunifyfs.la libunifyfs_api.la

libunifyfsdir = $(includedir)

if HAVE_GOTCHA
lib_LTLIBRARIES += libunifyfs_gotcha.la
libunifyfs_gotchadir = $(includedir)

if HAVE_FORTRAN
lib_LTLIBRARIES += libunifyfsf.la
endif #HAVE_FORTRAN
endif #HAVE_GOTCHA

AM_CFLAGS = -Wall -Wno-strict-aliasing -Werror

include_HEADERS = unifyfs.h $(UNIFYFS_COMMON_INSTALL_HDRS)
pkginclude_HEADERS = unifyfs_api.h

if HAVE_GOTCHA
if HAVE_FORTRAN
include_HEADERS += unifyfsf.h
endif #HAVE_FORTRAN
endif #HAVE_GOTCHA

CLIENT_COMMON_CPPFLAGS = \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/client/include \
  -I$(top_srcdir)/common/src

CLIENT_COMMON_CFLAGS = \
  $(AM_CFLAGS) \
  $(UNIFYFS_COMMON_FLAGS) \
  $(MPI_CFLAGS)

CLIENT_COMMON_LDFLAGS = \
  -version-info $(LIBUNIFYFS_LT_VERSION) \
  $(MPI_CLDFLAGS)

CLIENT_COMMON_LIBADD = \
  $(UNIFYFS_COMMON_LIBS) \
  -lm -lrt -lcrypto -lpthread

CLIENT_API_SRC_FILES = \
  unifyfs_api.h \
  unifyfs_api_internal.h \
  unifyfs_api.c \
  unifyfs_api_file.c \
  unifyfs_api_io.c \
  unifyfs_api_transfer.c

CLIENT_CORE_SRC_FILES = \
  $(OPT_SRCS) \
  $(UNIFYFS_COMMON_SRCS) \
  client_read.c \
  client_read.h \
  client_transfer.c \
  client_transfer.h \
  margo_client.c \
  margo_client.h \
  unifyfs.c \
  unifyfs.h \
  unifyfs-fixed.c \
  unifyfs-fixed.h \
  unifyfs-internal.h \
  uthash.h \
  utlist.h

if USE_PMPI_WRAPPERS
CLIENT_CORE_SRC_FILES += pmpi_wrappers.c pmpi_wrappers.h
endif

CLIENT_POSIX_SRC_FILES = \
  unifyfs-dirops.c \
  unifyfs-dirops.h \
  unifyfs-stdio.c \
  unifyfs-stdio.h \
  unifyfs-sysio.c \
  unifyfs-sysio.h

libunifyfs_api_la_SOURCES  = \
  $(CLIENT_API_SRC_FILES) \
  $(CLIENT_CORE_SRC_FILES) \
  $(CLIENT_POSIX_SRC_FILES)
libunifyfs_api_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_api_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_api_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_api_la_LIBADD   = $(CLIENT_COMMON_LIBADD)

libunifyfs_la_SOURCES  = \
  $(CLIENT_CORE_SRC_FILES) \
  $(CLIENT_POSIX_SRC_FILES)
libunifyfs_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_la_LIBADD   = $(CLIENT_COMMON_LIBADD)

if HAVE_GOTCHA

libunifyfs_gotcha_la_SOURCES  = \
  $(CLIENT_CORE_SRC_FILES) \
  $(CLIENT_POSIX_SRC_FILES) \
  gotcha_map_unifyfs_list.c
libunifyfs_gotcha_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS) -DUNIFYFS_GOTCHA
libunifyfs_gotcha_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS) $(GOTCHA_CFLAGS)
libunifyfs_gotcha_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS) $(GOTCHA_LDFLAGS)
libunifyfs_gotcha_la_LIBADD   = $(CLIENT_COMMON_LIBADD) -lgotcha

endif

if HAVE_FORTRAN

libunifyfsf_la_SOURCES  = unifyfsf.c
libunifyfsf_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfsf_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfsf_la_LIBADD   = libunifyfs_gotcha.la

endif
