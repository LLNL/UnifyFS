UNIFYFS_COMMON_INSTALL_HDRS = \
  %reldir%/unifyfs_configurator.h \
  %reldir%/unifyfs_const.h \
  %reldir%/unifyfs_rc.h

UNIFYFS_COMMON_BASE_SRCS = \
  %reldir%/arraylist.h \
  %reldir%/arraylist.c \
  %reldir%/compare_fn.h \
  %reldir%/compare_fn.c \
  %reldir%/ini.h \
  %reldir%/ini.c \
  %reldir%/rm_enumerator.h \
  %reldir%/rm_enumerator.c \
  %reldir%/seg_tree.h \
  %reldir%/seg_tree.c \
  %reldir%/slotmap.h \
  %reldir%/slotmap.c \
  %reldir%/tinyexpr.h \
  %reldir%/tinyexpr.c \
  %reldir%/tree.h \
  %reldir%/unifyfs_client.h \
  %reldir%/unifyfs_const.h \
  %reldir%/unifyfs_configurator.h \
  %reldir%/unifyfs_configurator.c \
  %reldir%/unifyfs_keyval.h \
  %reldir%/unifyfs_keyval.c \
  %reldir%/unifyfs_log.h \
  %reldir%/unifyfs_log.c \
  %reldir%/unifyfs_logio.h \
  %reldir%/unifyfs_logio.c \
  %reldir%/unifyfs_meta.h \
  %reldir%/unifyfs_meta.c \
  %reldir%/unifyfs_misc.c \
  %reldir%/unifyfs_misc.h \
  %reldir%/unifyfs_rpc_util.h \
  %reldir%/unifyfs_rpc_util.c \
  %reldir%/unifyfs_rpc_types.h \
  %reldir%/unifyfs_client_rpcs.h \
  %reldir%/unifyfs_server_rpcs.h \
  %reldir%/unifyfs_rc.h \
  %reldir%/unifyfs_rc.c \
  %reldir%/unifyfs_shm.h \
  %reldir%/unifyfs_shm.c \
  %reldir%/unifyfs-stack.h \
  %reldir%/unifyfs-stack.c

UNIFYFS_COMMON_BASE_FLAGS = \
  -DSYSCONFDIR="$(sysconfdir)" \
  $(MARGO_CFLAGS) $(OPENSSL_CFLAGS)

UNIFYFS_COMMON_BASE_LIBS = \
  $(MARGO_LDFLAGS) $(MARGO_LIBS) $(OPENSSL_LIBS) -lmercury_util \
  -lm -lrt -lcrypto -lpthread

UNIFYFS_COMMON_OPT_FLAGS =
UNIFYFS_COMMON_OPT_LIBS =
UNIFYFS_COMMON_OPT_SRCS =

if USE_PMIX
  UNIFYFS_COMMON_OPT_FLAGS += -DUSE_PMIX
  UNIFYFS_COMMON_OPT_LIBS += -lpmix
endif

if USE_PMI2
  UNIFYFS_COMMON_OPT_FLAGS += -DUSE_PMI2
  UNIFYFS_COMMON_OPT_LIBS += -lpmi2
endif

UNIFYFS_COMMON_SRCS = \
  $(UNIFYFS_COMMON_BASE_SRCS) \
  $(UNIFYFS_COMMON_OPT_SRCS)

UNIFYFS_COMMON_FLAGS = \
  $(UNIFYFS_COMMON_BASE_FLAGS) \
  $(UNIFYFS_COMMON_OPT_FLAGS)

UNIFYFS_COMMON_LIBS = \
  $(UNIFYFS_COMMON_BASE_LIBS) \
  $(UNIFYFS_COMMON_OPT_LIBS)
