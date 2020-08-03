lib_LTLIBRARIES = libunifyfs_common.la

include_HEADERS = unifyfs_const.h unifyfs_rc.h

libunifyfs_commondir = $(includedir)

BASE_SRCS = \
  ini.h \
  ini.c \
  cm_enumerator.h \
  cm_enumerator.c \
  rm_enumerator.h \
  rm_enumerator.c \
  seg_tree.h \
  seg_tree.c \
  slotmap.h \
  slotmap.c \
  tinyexpr.h \
  tinyexpr.c \
  tree.h \
  unifyfs_const.h \
  unifyfs_configurator.h \
  unifyfs_configurator.c \
  unifyfs_keyval.h \
  unifyfs_keyval.c \
  unifyfs_log.h \
  unifyfs_log.c \
  unifyfs_logio.h \
  unifyfs_logio.c \
  unifyfs_meta.h \
  unifyfs_meta.c \
  unifyfs_misc.c \
  unifyfs_misc.h \
  unifyfs_rpc_util.h \
  unifyfs_rpc_util.c \
  unifyfs_client_rpcs.h \
  unifyfs_server_rpcs.h \
  unifyfs_rc.h \
  unifyfs_rc.c \
  unifyfs_shm.h \
  unifyfs_shm.c

OPT_FLAGS =
OPT_LIBS =
OPT_SRCS =

if USE_PMIX
  OPT_FLAGS += -DUSE_PMIX
  OPT_LIBS += -lpmix
endif

if USE_PMI2
  OPT_FLAGS += -DUSE_PMI2
  OPT_LIBS += -lpmi2
endif

libunifyfs_common_la_SOURCES = $(BASE_SRCS) $(OPT_SRCS)

libunifyfs_common_la_CPPFLAGS = \
  $(OPT_FLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS) \
  $(MARGO_CFLAGS)

libunifyfs_common_la_CPPFLAGS += -DSYSCONFDIR="$(sysconfdir)"

libunifyfs_common_la_LDFLAGS = \
  -version-info $(LIBUNIFYFS_LT_VERSION)

libunifyfs_common_la_LIBADD = \
  $(OPT_LIBS) -lm -lrt -lcrypto -lpthread

AM_CFLAGS = -Wall -Werror -Wno-strict-aliasing
