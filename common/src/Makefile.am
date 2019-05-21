lib_LTLIBRARIES = libunifycr_common.la

include_HEADERS = unifycr_const.h err_enumerator.h

libunifycr_commondir = $(includedir)

BASE_SRCS = \
  ini.h \
  ini.c \
  err_enumerator.h \
  err_enumerator.c \
  cm_enumerator.h \
  cm_enumerator.c \
  rm_enumerator.h \
  rm_enumerator.c \
  flatbuffers_common_builder.h \
  flatbuffers_common_reader.h \
  ucr_read_builder.h \
  ucr_read_reader.h \
  tinyexpr.h \
  tinyexpr.c \
  unifycr_const.h \
  unifycr_configurator.h \
  unifycr_configurator.c \
  unifycr_keyval.h \
  unifycr_keyval.c \
  unifycr_log.h \
  unifycr_log.c \
  unifycr_meta.h \
  unifycr_rpc_util.h \
  unifycr_rpc_util.c \
  unifycr_client_rpcs.h \
  unifycr_server_rpcs.h \
  unifycr_runstate.h \
  unifycr_runstate.c \
  unifycr_shm.h \
  unifycr_shm.c

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

libunifycr_common_la_SOURCES = $(BASE_SRCS) $(OPT_SRCS)

libunifycr_common_la_CPPFLAGS = \
  $(OPT_FLAGS) \
  $(MERCURY_CFLAGS) \
  $(ARGOBOTS_CFLAGS) \
  $(MARGO_CFLAGS) \
  $(FLATCC_CFLAGS)

libunifycr_common_la_LDFLAGS = \
  -version-info $(LIBUNIFYCR_LT_VERSION)

libunifycr_common_la_LIBADD = \
  $(OPT_LIBS) -lm -lrt

AM_CFLAGS = -Wall -Wno-strict-aliasing
