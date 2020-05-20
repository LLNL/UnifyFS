libexec_PROGRAMS = unifyfs-stage

unifyfs_stage_SOURCES = unifyfs-stage.c \
                        unifyfs-stage-transfer.c

noinst_HEADERS = unifyfs-stage.h

unifyfs_stage_CPPFLAGS = $(AM_CPPFLAGS) $(MPI_CFLAGS) \
                         $(OPENSSL_CFLAGS) \
                         -I$(top_srcdir)/client/src \
                         -I$(top_srcdir)/common/src

unifyfs_stage_LDADD = $(top_builddir)/client/src/libunifyfs.la -lrt -lm

unifyfs_stage_LDFLAGS = -static $(CP_WRAPPERS) $(AM_LDFLAGS) \
                        $(MPI_CLDFLAGS) $(FLATCC_LDFLAGS) $(FLATCC_LIBS) \
                        $(OPENSSL_LIBS)

AM_CFLAGS = -Wall -Werror

CLEANFILES = $(libexec_PROGRAMS)
