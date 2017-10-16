#METASTORE_HOME=/p/lscratchf/wang86/MetaStore/src
LEVELDB_HOME=/g/g0/sikich1/leveldb-1.14

METASTORE_HOME=../BurstFS_Meta/src

all: burstfsd 

burstfsd:
	mpicc -g -O0 -Wall arraylist.c burstfs_init.c burstfs_cmd_handler.c  \
		burstfs_debug.c burstfs_const.c burstfs_metadata.c \
		burstfs_request_manager.c burstfs_service_manager.c burstfs_sock.c \
		-I${METASTORE_HOME}/Mlog/ -I${METASTORE_HOME}/uthash -I${METASTORE_HOME} \
		-I${LEVELDB_HOME}/include -L${LEVELDB_HOME} -L${METASTORE_HOME} -DLEVELDB_SUPPORT \
		-lmdhim -lm -pthread -lstdc++ -lrt -lleveldb -o burstfsd 

clean:
	rm burstfsd 

