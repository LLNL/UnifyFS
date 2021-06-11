include $(top_srcdir)/common/src/Makefile.mk

lib_LTLIBRARIES = libunifyfs.la libunifyfs_api.la

if USE_PMPI_WRAPPERS
lib_LTLIBRARIES += libunifyfs_mpi.la
endif #USE_PMPI_WRAPPERS

if HAVE_GOTCHA
lib_LTLIBRARIES += libunifyfs_gotcha.la

if USE_PMPI_WRAPPERS
lib_LTLIBRARIES += libunifyfs_mpi_gotcha.la
endif #USE_PMPI_WRAPPERS

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


# Common compile/link flag definitions

CLIENT_COMMON_CPPFLAGS = \
  -I$(top_builddir)/client \
  -I$(top_srcdir)/client/include \
  -I$(top_srcdir)/common/src

CLIENT_COMMON_CFLAGS = \
  $(AM_CFLAGS) \
  $(UNIFYFS_COMMON_FLAGS) \
  $(MPI_CFLAGS)

CLIENT_COMMON_LDFLAGS = \
  -version-info $(LIBUNIFYFS_LT_VERSION)

CLIENT_COMMON_LIBADD = \
  $(UNIFYFS_COMMON_LIBS) \
  $(MPI_CLDFLAGS) \
  -lm -lrt -lcrypto -lpthread

if HAVE_SPATH
CLIENT_COMMON_CFLAGS += $(SPATH_CFLAGS)
CLIENT_COMMON_LIBADD += $(SPATH_LIBS)
endif

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

CLIENT_POSIX_SRC_FILES = \
  unifyfs-dirops.c \
  unifyfs-dirops.h \
  unifyfs-stdio.c \
  unifyfs-stdio.h \
  unifyfs-sysio.c \
  unifyfs-sysio.h

PMPI_SRC_FILES = \
  pmpi_wrappers.c \
  pmpi_wrappers.h


# Per-target flags begin here

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

if USE_PMPI_WRAPPERS
libunifyfs_mpi_la_SOURCES  = $(PMPI_SRC_FILES)
libunifyfs_mpi_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_mpi_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_mpi_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_mpi_la_LIBADD   = libunifyfs.la
endif #USE_PMPI_WRAPPERS


if HAVE_GOTCHA

libunifyfs_gotcha_la_SOURCES  = \
  $(CLIENT_CORE_SRC_FILES) \
  $(CLIENT_POSIX_SRC_FILES) \
  gotcha_map_unifyfs_list.c
libunifyfs_gotcha_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS) -DUNIFYFS_GOTCHA
libunifyfs_gotcha_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS) $(GOTCHA_CFLAGS)
libunifyfs_gotcha_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_gotcha_la_LIBADD   = $(CLIENT_COMMON_LIBADD) $(GOTCHA_LIBS)

if USE_PMPI_WRAPPERS
libunifyfs_mpi_gotcha_la_SOURCES  = $(PMPI_SRC_FILES)
libunifyfs_mpi_gotcha_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfs_mpi_gotcha_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfs_mpi_gotcha_la_LDFLAGS  = $(CLIENT_COMMON_LDFLAGS)
libunifyfs_mpi_gotcha_la_LIBADD   = libunifyfs_gotcha.la
endif #USE_PMPI_WRAPPERS

if HAVE_FORTRAN
libunifyfsf_la_SOURCES  = unifyfsf.c
libunifyfsf_la_CPPFLAGS = $(CLIENT_COMMON_CPPFLAGS)
libunifyfsf_la_CFLAGS   = $(CLIENT_COMMON_CFLAGS)
libunifyfsf_la_LIBADD   = libunifyfs_gotcha.la
endif #HAVE_FORTRAN

endif #HAVE_GOTCHA
