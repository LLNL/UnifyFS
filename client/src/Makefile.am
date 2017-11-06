lib_LTLIBRARIES = libunifycr.la
libunifycrdir = $(includedir)

libunifycr_la_SOURCES = unifycr-fixed.c unifycr-stack.c \
                        unifycr-stdio.c unifycr-sysio.c

libunifycr_la_CPPFLAGS = -I$(top_srcdir)/client

libunifycr_la_LDFLAGS = -version-info $(LIBUNIFYCR_LT_VERSION)

# TODO: this public header file is broken. It requires unifycr-internal.h.
libunifycr_HEADERS = unifycr.h

