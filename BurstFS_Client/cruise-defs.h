#define CRUISE_MAX_FILES        ( 128 )

/* eventually could decouple these so there could be
 * more or less file descriptors than files, but for
 * now they're the same */
#define CRUISE_MAX_FILEDESCS    ( CRUISE_MAX_FILES )

#define CRUISE_MAX_FILENAME     ( 128 )

#define CRUISE_STREAM_BUFSIZE   ( 1 * 1024 * 1024 )

#define CRUISE_CHUNK_BITS       ( 24 )

#ifdef MACHINE_BGQ
  #define CRUISE_CHUNK_MEM      ( 64 * 1024 * 1024 )
#else /* MACHINE_BGQ */
  #define CRUISE_CHUNK_MEM      ( 256 * 1024 * 1024 )
#endif /* MACHINE_BGQ */

#define CRUISE_SPILLOVER_SIZE   ( 1 * 1024 * 1024 * 1024 )

#define CRUISE_SUPERBLOCK_KEY   ( 4321 )

// const for burstfs
#define GEN_STR_LEN 1024
#define SOCKET_PATH "/tmp/burstfs_server_sock"
#define CRUISE_DEF_REQ_SIZE 1024*1024*8*16 + 131072
#define CRUISE_DEF_RECV_SIZE 1024*1024 + 131072
#define BURSTFS_INDEX_BUF_SIZE	(20*1024*1024)
#define BURSTFS_FATTR_BUF_SIZE 1024*1024
#define BURSTFS_MAX_SPLIT_CNT 1048576
#define BURSTFS_MAX_READ_CNT 1048576
