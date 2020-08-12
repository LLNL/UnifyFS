if USE_MDHIM
noinst_LIBRARIES = libmdhim.a
endif

libmdhim_a_SOURCES = Mlog2/mlog2.c \
                     Mlog2/mlog2.h \
                     Mlog2/mlogfacs2.h \
                     client.c \
                     client.h \
                     local_client.c \
                     local_client.h \
                     data_store.c \
                     data_store.h \
                     partitioner.c \
                     partitioner.h \
                     messages.c \
                     messages.h \
                     range_server.c \
                     range_server.h \
                     ds_leveldb.c \
                     ds_leveldb.h \
                     mdhim_options.c \
                     mdhim_options.h \
                     mdhim_private.c \
                     mdhim_private.h \
                     indexes.c \
                     indexes.h \
                     mdhim.c \
                     mdhim.h \
                     uthash/uthash.h

# target_LDFLAGS is not valid for library archive targets
#libmdhim_a_LDFLAGS = $(AM_LDFLAGS) $(LEVELDB_LDFLAGS) $(LEVELDB_LIBS) $(MPI_CLDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/meta/src/Mlog2 \
              -I$(top_srcdir)/meta/src/uthash \
              -I$(top_srcdir)/common/src \
              -I$(top_srcdir)/server/src

AM_CFLAGS = -DLEVELDB_SUPPORT $(LEVELDB_CFLAGS) $(MPI_CFLAGS) $(MARGO_CFLAGS)
AM_CFLAGS += -Wall -Werror

CLEANFILES =
