lib_LTLIBRARIES = libunifycr_common.la

libunifycr_commondir = $(includedir)

libunifycr_common_la_SOURCES = \
  ini.h \
  ini.c \
  err_enumerator.h \
  err_enumerator.c \
  cm_enumerator.h \
  cm_enumerator.c \
  rm_enumerator.h \
  rm_enumerator.c \
  tinyexpr.h \
  tinyexpr.c \
  unifycr_const.h \
  unifycr_configurator.h \
  unifycr_configurator.c \
  unifycr_runstate.h \
  unifycr_runstate.c \
  unifycr_meta.h

libunifycr_common_la_CPPFLAGS = -I$(top_builddir)/client
libunifycr_common_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION)
libunifycr_common_la_LIBADD = -lm

AM_CFLAGS = -Wall -Wno-strict-aliasing
