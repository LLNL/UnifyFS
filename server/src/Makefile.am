bin_PROGRAMS = unifycrd

unifycrd_SOURCES = arraylist.c \
                   unifycr_cmd_handler.c \
                   unifycr_const.c \
                   unifycr_debug.c \
                   unifycr_init.c \
                   unifycr_metadata.c \
                   unifycr_request_manager.c \
                   unifycr_service_manager.c \
                   unifycr_sock.c

unifycrd_LDFLAGS = -static

unifycrd_LDADD = $(top_srcdir)/meta/src/libmdhim.a \
                 -lpthread -lm -lstdc++ -lrt \
                 $(LEVELDB_LIBS)

noinst_HEADERS = arraylist.h \
                 unifycr_cmd_handler.h \
                 unifycr_const.h \
                 unifycr_debug.h \
                 unifycr_init.h \
                 unifycr_metadata.h \
                 unifycr_request_manager.h \
                 unifycr_service_manager.h \
                 unifycr_sock.h

AM_CPPFLAGS = -I$(top_srcdir)/meta/src \
              -I$(top_srcdir)/meta/src/uthash \
              -I$(top_srcdir)/meta/src/Mlog2

AM_CFLAGS = -Wall

CLEANFILES = $(bin_PROGRAMS)
