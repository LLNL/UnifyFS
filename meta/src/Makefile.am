noinst_LIBRARIES = libmdhim.a

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

AM_CPPFLAGS = -I$(top_srcdir)/meta/src/Mlog2 \
              -I$(top_srcdir)/meta/src/uthash

AM_CFLAGS = -DLEVELDB_SUPPORT $(LEVELDB_CFLAGS)
AM_CFLAGS += -Wall

CLEANFILES =
