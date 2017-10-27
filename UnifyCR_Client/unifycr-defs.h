#define UNIFYCR_MAX_FILES        ( 128 )

/* eventually could decouple these so there could be
 * more or less file descriptors than files, but for
 * now they're the same */
#define UNIFYCR_MAX_FILEDESCS    ( UNIFYCR_MAX_FILES )

#define UNIFYCR_MAX_FILENAME     ( 128 )

#define UNIFYCR_STREAM_BUFSIZE   ( 1 * 1024 * 1024 )

#define UNIFYCR_CHUNK_BITS       ( 24 )

#ifdef MACHINE_BGQ
  #define UNIFYCR_CHUNK_MEM      ( 64 * 1024 * 1024 )
#else /* MACHINE_BGQ */
  #define UNIFYCR_CHUNK_MEM      ( 256 * 1024 * 1024 )
#endif /* MACHINE_BGQ */

#define UNIFYCR_SPILLOVER_SIZE   ( 1 * 1024 * 1024 * 1024 )

#define UNIFYCR_SUPERBLOCK_KEY   ( 4321 )

// const for unifycr
#define GEN_STR_LEN 1024
#define SOCKET_PATH "/tmp/unifycr_server_sock"
#define UNIFYCR_DEF_REQ_SIZE 1024*1024*8*16 + 131072
#define UNIFYCR_DEF_RECV_SIZE 1024*1024 + 131072
#define UNIFYCR_INDEX_BUF_SIZE	(20*1024*1024)
#define UNIFYCR_FATTR_BUF_SIZE 1024*1024
#define UNIFYCR_MAX_SPLIT_CNT 1048576
#define UNIFYCR_MAX_READ_CNT 1048576
