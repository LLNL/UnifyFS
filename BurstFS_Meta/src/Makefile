include ../Makefile.cfg

ifeq ($(FORTRAN),1)
	OBJS    = mlog2.o client.o local_client.o data_store.o partitioner.o messages.o range_server.o mdhim_options.o mdhim_private.o indexes.o mdhim_fortran.o  mdhim_f90_binding.o
else
	OBJS    = mlog2.o client.o local_client.o data_store.o partitioner.o messages.o range_server.o mdhim_options.o mdhim_private.o indexes.o
endif

ifeq ($(LEVELDB),1)
	OBJS += ds_leveldb.o
else
	ifeq ($(ROCKSDB),1)
		OBJS += ds_leveldb.o
	endif
endif

ifeq ($(MYSQLDB),1)
	OBJS += ds_mysql.o
endif


all:	mdhim.o lib

mdhim.o: mdhim.c $(OBJS)
	$(CC) -c $< $(CINC) $(CLIBS) -lleveldb

mdhim_fortran.o: mdhim_fortran.c
	$(CC) -c $< $(CINC) $(CLIBS)

mlog2.o: Mlog2/mlog2.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

mdhim_private.o: mdhim_private.c
	$(CC) -c $< $(CINC) $(CLIBS) -lleveldb

client.o: client.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

local_client.o: local_client.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

messages.o: messages.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

partitioner.o: partitioner.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

indexes.o: indexes.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

data_store.o: data_store.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

ds_mysql.o: ds_mysql.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

ds_leveldb.o: ds_leveldb.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

ds_sophia.o: ds_sophia.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

range_server.o: range_server.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

mdhim_options.o : mdhim_options.c
	$(CC) -c $^ $(CINC) $(CLIBS) -lleveldb

mdhim_f90_binding.o: mdhim_f90_binding.f90
	$(FF) -c $< $(FINC) $(FLIBS)

lib:	
	ar rvs libmdhim.a mdhim.o $(OBJS)
dylib:
	$(CC) -shared -fPIC -o libmdhim.so.1 mdhim.o $(OBJS) -L$(LEVEL_DB_ROOT)/ -lleveldb

clean:
	rm -rf *.o libmdhim.a libmdhim.so.1 libmdhimf90.so.1
