#METASTORE_HOME=/p/lscratchf/wang86/MetaStore/src
#LEVELDB_HOME=/g/g92/wang86/leveldb/leveldb

METASTORE_HOME=../BurstFS_Meta/src

all: server

server:
	mpicc -g -O2 -Wall arraylist.c burstfs_init.c burstfs_cmd_handler.c  \
		burstfs_debug.c burstfs_const.c burstfs_metadata.c \
		burstfs_request_manager.c burstfs_service_manager.c burstfs_sock.c \
		-I${METASTORE_HOME}/Mlog/ -I${METASTORE_HOME}/uthash -I${METASTORE_HOME} \
		-I${LEVELDB_HOME}/include -L${METASTORE_HOME} -DLEVELDB_SUPPORT \
		-lmdhim -lm -pthread -lstdc++ -lrt -lleveldb -o server

clean:
	rm server 

