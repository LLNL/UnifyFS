#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "mpi.h"
#include "mdhim.h"

//#define KEYS 100
//#define TOTAL 100000000
#define GEN_STR_LEN 1024

extern double memgettime, ssdgettime, get_other_time, find_table_time;
extern double imemgettime, maytime, get_result_time;
extern double synctime, myseektime, roomtime, \
batchtime, key_put_time, addtime, \
bgtime, bartime, nbstoretime, newnodetime,\
 nbnexttime, findtime, judgetime, \
heighttime, allfindtime, recordtime, \
inserttime, waittime, block_reader_time, \
seek_result_time, get_match_time, get_decode_time;
extern double valassigntime, keyptrtime, get_internal_time, find_tale_time, seek_iter_time;

extern long nr_mem_get, nr_ssd_get, nr_hit, nr_miss;
extern double mem_get_time, ssd_get_time, cache_get_time, \
  read_compact_time, read_cache_time, read_block_time, \
    seek_block_time, mem_put_time, writeahead_time, \
      write_compact_time, sstable_time, tablecache_time;


typedef struct {
  unsigned long fid;
  unsigned long nodeid;

  unsigned long offset;
  unsigned long addr;
  unsigned long len;
}meta_t;

typedef struct {
  unsigned long fid;
  unsigned long offset;
}ulfs_key_t;

typedef struct {
  unsigned long nodeid;
  unsigned long len;
  unsigned long addr;
}ulfs_val_t;

extern double localgetcpytime;
extern double localrangetime;
extern double localbpmtime;
extern double localmalloctime;
extern double resp_get_comm_time;
extern double resp_put_comm_time;
extern double msgputtime;
extern double msggettime;
extern double dbgettime;
extern double dbbputtime;
extern double localcpytime;
extern double packputtime;
extern double packgettime;
extern double stat_time;
extern double packretgettime;
extern double packretputtime;
extern double packmpiputtime;
extern double localassigntime;

int init_meta_lst(meta_t *meta_lst, ulfs_key_t **key_lst,\
	 ulfs_val_t **value_lst, long segnum, long transz, int rank, \
		int size);


int main(int argc, char **argv) {
	int ret;
	int provided;
	struct mdhim_t *md;
	int **keys;
	int **values;
	int total = 0;
	struct mdhim_brm_t *brm, *brmp;
	struct mdhim_bgetrm_t *bgrm, *bgrmp;
	struct timeval start_tv, end_tv, put_end;
	
	double  get_time = 0, put_time = 0;

	int c, serratio, bulknum, size, path_len;
	long transz, segnum, rangesz;
	char db_path[GEN_STR_LEN] = {0};
	char db_name[GEN_STR_LEN] = {0};
	int      dbug = MLOG_CRIT; //MLOG_CRIT=1, MLOG_DBG=2
	mdhim_options_t *db_opts; // Local variable for db create options to be passed
	int db_type = LEVELDB; //(data_store.h) 
	MPI_Comm comm;

	static const char *opts = "c:t:s:r:n:p:d:";
	
	while ((c = getopt(argc, argv, opts)) != -1) {
	switch(c) {
        case 'c':
            bulknum = atoi(optarg); break;
        case 's':
            serratio = atoi(optarg); break;
        case 't':
            transz = atol(optarg); break;
        case 'r':
            rangesz = atol(optarg); break;
        case 'n':
            segnum = atol(optarg); break;
        case 'p':
          strcpy(db_path, optarg); break;
        case 'd':
          strcpy(db_name, optarg); break;
      }
	}

	int *key_lens;
	int *val_lens;
        // Create options for DB initialization

	db_opts = malloc(sizeof(struct mdhim_options_t));
	
	db_opts->db_path = db_path;
	db_opts->db_create_new = 1;
	db_opts->db_value_append = MDHIM_DB_OVERWRITE;

	db_opts->rserver_factor = serratio;
	db_opts->max_recs_per_slice = 1024 * 1024 *50;
	db_opts->num_paths = 0;
	db_opts->num_wthreads = 1; 

	path_len = strlen(db_opts->db_path) \
		+ strlen("manifest") + 1;

	char *manifest_path;
	manifest_path = malloc(path_len);
	sprintf(manifest_path, "%s%s", db_opts->db_path, "manifest");
	db_opts->manifest_path = manifest_path;
	db_opts->db_name = db_name;
	db_opts->db_type = LEVELDB;
	db_opts->db_key_type = MDHIM_BURSTFS_KEY;
	db_opts->debug_level = MLOG_CRIT;
	db_opts->max_recs_per_slice = rangesz;
	db_opts->rserver_factor = serratio;	

	//Initialize MPI with multiple thread support
	ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (ret != MPI_SUCCESS) {
		printf("Error initializing MPI with threads\n");
		exit(1);
	}

	//Quit if MPI didn't initialize with multiple threads
	if (provided != MPI_THREAD_MULTIPLE) {
                printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
                exit(1);
        }

	//Initialize MDHIM
	comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);
	if (!md) {
		printf("Error initializing MDHIM\n");
		MPI_Abort(MPI_COMM_WORLD, ret);
		exit(1);
	}	

  meta_t *meta_lst = (meta_t *)malloc(segnum*sizeof(meta_t));
  ulfs_key_t **key_lst = \
		(ulfs_key_t **)malloc(segnum*sizeof(ulfs_key_t *));
  ulfs_val_t **val_lst = \
		(ulfs_val_t **)malloc(segnum * sizeof(ulfs_val_t *));

  key_lens = (int *)malloc(segnum * sizeof(int));
  val_lens = (int *)malloc(segnum * sizeof(int));

	long i;
	for (i = 0; i < segnum; i++) {
    key_lens[i] = sizeof(ulfs_key_t);
    val_lens[i] = sizeof(ulfs_val_t);
  }
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(md->mdhim_comm, &size);
	MPI_Barrier(MPI_COMM_WORLD);	

	init_meta_lst(meta_lst, key_lst, val_lst,\
		 segnum, transz, md->mdhim_rank, size);

	
	gettimeofday(&start_tv, NULL);
//	printf("rank:%d, mdihm_rank is %d\n", rank, md->mdhim_rank);
//	fflush(stdout);
	while (total != segnum) {
		//Insert the keys into MDHIM
		brm = mdhimBPut(md, (void **) (&key_lst[total]), key_lens,  
				(void **) (&val_lst[total]), val_lens, bulknum, 
				NULL, NULL);
		brmp = brm;
    if (!brmp || brmp->error) {
         printf("Rank - %d: Error inserting keys/values into\
					 MDHIM\n", md->mdhim_rank);
                } 
		while (brmp) {
			if (brmp->error < 0) {
				printf("Rank: %d - Error inserting key/values info MDHIM\n", md->mdhim_rank);
			}
	
			brmp = brmp->next;
			//Free the message
			mdhim_full_release_msg(brm);
			brm = brmp;
		}
	
		total += bulknum;
	}
	MPI_Barrier(MPI_COMM_WORLD);
	//Commit the database
	ret = mdhimCommit(md, md->primary_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error committing MDHIM database\n");
	} else {
//		printf("Committed MDHIM database\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
	total = 0;
	gettimeofday(&put_end, NULL);
	put_time = 1000000*(put_end.tv_sec-start_tv.tv_sec) + put_end.tv_usec-start_tv.tv_usec;
	while (total != segnum) {
		//Get the values back for each key inserted
		bgrm = mdhimBGet(md, md->primary_index,\
		 &key_lst[total], key_lens, 
				 bulknum, MDHIM_GET_EQ);
		bgrmp = bgrm;
		while (bgrmp) {
			if (bgrmp->error < 0) {
				printf("Rank: %d - Error retrieving values", md->mdhim_rank);
			}

			for (i = 0; i < bgrmp->num_keys && bgrmp->error >= 0; i++) {
		
//				printf("Rank: %d - Got key: %d value: %d\n", md->mdhim_rank, 
//				       *(int *)bgrmp->keys[i], *(int *)bgrmp->values[i]);
/*	     printf("Rank: %d -  num_keys is %ld, offset:%ld\n", md->mdhim_rank,
        bgrmp->num_keys, ((ulfs_key_t *)(bgrmp->keys[i]))->offset);
				fflush(stdout);
*/
			}

			bgrmp = bgrmp->next;
			//Free the message received
			mdhim_full_release_msg(bgrm);
			bgrm = bgrmp;
		}

		total += bulknum;
	}
	MPI_Barrier(MPI_COMM_WORLD);
	gettimeofday(&end_tv, NULL);
	get_time = 1000000*(end_tv.tv_sec - put_end.tv_sec) + end_tv.tv_usec - put_end.tv_usec;
	//Quit MDHIM
	ret = mdhimClose(md);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}
	

if (md->mdhim_rank == 0) {
/*	for (i = 0; i < KEYS; i++) {
		free(keys[i]);
		free(values[i]);
	}

	free(keys);
	free(values);
*/
}
	MPI_Barrier(MPI_COMM_WORLD);
	if(md->mdhim_rank == 0) {
		printf("put time is %lf, get time is %lf, \ 
			network_put is %lf, network_get is %lf, \ 
				,dbputtime:%lf, dbgettime:%lf, packputtime:%lf, \
					packgettime:%lf, localcpytime:%lf, \
						resp_put_comm_time:%lf, \
							resp_get_comm_time:%lf, \
								stat_time:%lf, packretputtime:%lf, \
								packretgettime:%lf, \
									localgetcpytime:%lf, \
										packmpiputtime:%lf, \
											localassigntime:%lf, \
												localmalloctime:%lf, \
														localbpmtime:%lf, \
															localrangetime:%lf\n", \
								put_time/1000000,\
					 get_time/1000000, \
						msgputtime/1000000, msggettime/1000000,\
							 dbbputtime/1000000, \
								dbgettime/1000000, packputtime/1000000, \
									packgettime/1000000, localcpytime/1000000, \
										resp_put_comm_time/1000000,	\
											resp_get_comm_time/1000000, \
												stat_time/1000000, \
													packretputtime/1000000, \
														packretgettime/1000000, \
															localgetcpytime/1000000, \
																packmpiputtime/1000000, \
																	localassigntime/1000000, \
																		localmalloctime/1000000, \
																			localbpmtime/1000000, \
																				localrangetime/1000000);

	printf("mem_get_time:%lf, ssd_get_time:%lf, \
		read_compact_time:%lf, read_cache_time:%lf, \
			read_block_time:%lf, seek_block_time:%lf, hit:%ld, miss:%ld, nr_mem_get:%ld, nr_ssd_get:%ld, sstabletime:%lf, tablecache_time:%lf, cache_get_time:%lf, get_other_time:%lf, getresulttime:%lf, findtime:%lf, internaltime:%lf, decodetime:%lf, seekresulttime:%lf, matchtime:%lf, blockreadtime:%lf, seekitertime:%lf\n\n", \
				mem_get_time/1000000, ssd_get_time/1000000, \
					read_compact_time/1000000, read_cache_time/1000000, \
						read_block_time/1000000, seek_block_time/1000000, nr_hit, \
							nr_miss, nr_mem_get, nr_ssd_get, sstable_time/1000000, \
								tablecache_time/1000000, cache_get_time/1000000, \
										get_other_time/1000000, \
											get_result_time/1000000, find_table_time/1000000,\
												get_internal_time/1000000,\
													 get_decode_time/1000000, \
															seek_result_time/1000000, \
																get_match_time/1000000, \
																	block_reader_time/1000000, \
																		seek_iter_time/1000000);

  printf("mem_put_time:%lf, writeahead_time:%lf, \
    write_compact_time:%lf\n", mem_put_time/1000000, \
      writeahead_time/1000000, write_compact_time/1000000);


		fflush(stdout);
//	printf("Took: %u seconds to insert and get %u keys/values\n", 
//	       (unsigned int) (end_tv.tv_sec - start_tv.tv_sec), TOTAL);
}
	MPI_Finalize();
	return 0;
}

int init_meta_lst(meta_t *meta_lst, ulfs_key_t **key_lst,\
	 ulfs_val_t **value_lst, \
    long segnum, long transz, int rank, int size) {

  long i=0;
  for (i = 0; i < segnum; i++) {
    meta_lst[i].fid = 0;

//    meta_lst[i].offset = i * transz + rank * transz * segnum;
    meta_lst[i].offset = i * transz * size + rank * transz;
    meta_lst[i].nodeid = rank;
    meta_lst[i].addr = i * transz;
    meta_lst[i].len = transz;
    fflush(stdout);

    key_lst[i] = (ulfs_key_t *)malloc(sizeof(ulfs_key_t));
    key_lst[i]->fid = meta_lst[i].fid;
    key_lst[i]->offset = meta_lst[i].offset;

    value_lst[i] = (ulfs_val_t *)malloc(sizeof(ulfs_val_t));
    value_lst[i]->addr = meta_lst[i].addr;
    value_lst[i]->len = meta_lst[i].len;
    value_lst[i]->nodeid = meta_lst[i].nodeid;
  }
	return 0;
}
