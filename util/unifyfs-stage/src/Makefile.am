bin_PROGRAMS = unifyfs-stage

noinst_HEADERS = unifyfs-stage.h

CLEANFILES = $(libexec_PROGRAMS)

# Compiler/linker flags

AM_CFLAGS = -Wall -Werror

stage_cppflags = $(AM_CPPFLAGS) $(MPI_CFLAGS) \
   $(OPENSSL_CFLAGS) \
   -I$(top_srcdir)/client/src \
   -I$(top_srcdir)/common/src

stage_unify_lib = $(top_builddir)/client/src/libunifyfs_api.la

stage_ldadd = \
  $(stage_unify_lib) \
  -lrt -lm \
  $(OPENSSL_LIBS) \
  $(MPI_CLDFLAGS)

stage_ldflags = \
  $(AM_LDFLAGS) \
  -static

# Per-target flags begin here

stage_sources = \
  unifyfs-stage.c \
  unifyfs-stage-transfer.c

unifyfs_stage_CPPFLAGS = $(stage_cppflags)
unifyfs_stage_LDADD    = $(stage_ldadd)
unifyfs_stage_LDFLAGS  = $(stage_ldflags)
unifyfs_stage_SOURCES  = $(stage_sources)


