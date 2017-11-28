noinst_LIBRARIES = libmdhim.a

libmdhim_a_SOURCES = Mlog2/mlog2.c client.c local_client.c data_store.c \
                     partitioner.c messages.c range_server.c ds_leveldb.c \
                     mdhim_options.c mdhim_private.c indexes.c mdhim.c

AM_CPPFLAGS = -I$(top_srcdir)/meta/src/Mlog2 \
              -I$(top_srcdir)/meta/src/uthash

AM_CFLAGS = -DLEVELDB_SUPPORT $(LEVELDB_CFLAGS)

CLEANFILES = 
