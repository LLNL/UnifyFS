include $(top_srcdir)/common/src/Makefile.mk

bin_PROGRAMS = unifyfsd

AM_CPPFLAGS = \
  -I$(top_srcdir)/common/src \
  -I$(top_srcdir)/client/src

AM_CFLAGS = -Wall -Werror

unifyfsd_SOURCES = \
  $(UNIFYFS_COMMON_SRCS) \
  extent_tree.c \
  extent_tree.h \
  margo_server.c \
  margo_server.h \
  unifyfs_client_rpc.c \
  unifyfs_fops.h \
  unifyfs_global.h \
  unifyfs_group_rpc.h \
  unifyfs_group_rpc.c \
  unifyfs_inode.h \
  unifyfs_inode.c \
  unifyfs_inode_tree.h \
  unifyfs_inode_tree.c \
  unifyfs_metadata_mdhim.h \
  unifyfs_p2p_rpc.h \
  unifyfs_p2p_rpc.c \
  unifyfs_request_manager.c \
  unifyfs_request_manager.h \
  unifyfs_server.c \
  unifyfs_service_manager.c \
  unifyfs_service_manager.h \
  unifyfs_server_pid.c \
  unifyfs_transfer.c \
  unifyfs_transfer.h \
  unifyfs_tree.c \
  unifyfs_tree.h

OPT_CPP_FLAGS =
OPT_C_FLAGS =
OPT_LD_FLAGS =
OPT_LIBS =

if USE_MDHIM

  unifyfsd_SOURCES += \
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

  unifyfsd_SOURCES += unifyfs_fops_rpc.c

endif # USE_MDHIM

if USE_PMIX
  OPT_C_FLAGS += -DUSE_PMIX
  OPT_LIBS += -lpmix
endif

if USE_PMI2
  OPT_C_FLAGS += -DUSE_PMI2
  OPT_LIBS += -lpmi2
endif

unifyfsd_CFLAGS  = $(AM_CFLAGS) $(UNIFYFS_COMMON_FLAGS) $(OPT_C_FLAGS)
unifyfsd_LDFLAGS = $(OPT_LD_FLAGS)
unifyfsd_LDADD   = $(UNIFYFS_COMMON_LIBS) $(OPT_LIBS)

CLEANFILES = $(bin_PROGRAMS)
