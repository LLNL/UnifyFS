bin_PROGRAMS = unifyfsd

noinst_LIBRARIES = libunifyfsd.a

unifyfsd_SOURCES = unifyfs_server.c

libunifyfsd_a_SOURCES = \
    extent_tree.c \
    extent_tree.h \
    margo_server.c \
    margo_server.h \
    unifyfs_cmd_handler.c \
    unifyfs_fops.h \
    unifyfs_global.h \
    unifyfs_group_rpc.h \
    unifyfs_group_rpc.c \
    unifyfs_inode.h \
    unifyfs_inode.c \
    unifyfs_inode_tree.h \
    unifyfs_inode_tree.c \
    unifyfs_metadata_mdhim.h \
    unifyfs_request_manager.c \
    unifyfs_request_manager.h \
    unifyfs_service_manager.c \
    unifyfs_service_manager.h \
    unifyfs_server_pid.c \
    unifyfs_tree.c \
    unifyfs_tree.h

OPT_CPP_FLAGS =
OPT_C_FLAGS =
OPT_LD_FLAGS =
OPT_LIBS =

if USE_MDHIM

  libunifyfsd_a_SOURCES += \
    unifyfs_metadata_mdhim.c \
    unifyfs_fops_mdhim.c

  OPT_CPP_FLAGS += \
      -DUSE_MDHIM \
      -I$(top_srcdir)/meta/src \
      -I$(top_srcdir)/meta/src/uthash \
      -I$(top_srcdir)/meta/src/Mlog2

  OPT_C_FLAGS += \
      $(LEVELDB_CFLAGS) \
      $(MPI_CFLAGS)

  OPT_LD_FLAGS += \
      $(LEVELDB_LDFLAGS) \
      $(MPI_CLDFLAGS)

  OPT_LIBS += \
      $(top_builddir)/meta/src/libmdhim.a \
      $(LEVELDB_LIBS)

else # ! USE_MDHIM

  libunifyfsd_a_SOURCES += \
    unifyfs_fops_rpc.c

endif # USE_MDHIM


unifyfsd_LDFLAGS = -static \
  $(OPT_LD_FLAGS) \
  $(MARGO_LDFLAGS) \
  $(FLATCC_LDFLAGS)

unifyfsd_LDADD = \
    libunifyfsd.a \
    $(top_builddir)/common/src/libunifyfs_common.la \
    $(OPT_LIBS) \
    $(MARGO_LIBS) \
    $(FLATCC_LIBS) \
    -lpthread -lm -lstdc++ -lrt

AM_CPPFLAGS = \
  $(OPT_CPP_FLAGS) \
  -I$(top_srcdir)/common/src \
  -I$(top_srcdir)/client/src

AM_CFLAGS = -Wall -Werror \
  $(OPT_C_FLAGS) \
  $(FLATCC_CFLAGS) \
  $(MARGO_CFLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS)

CLEANFILES = $(bin_PROGRAMS)
