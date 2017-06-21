/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 * Written by
 * 	Teng Wang tw15g@my.fsu.edu
 * 	Adam Moody moody20@llnl.gov
 * 	Weikuan Yu wyu3@fsu.edu
 * 	Kento Sato kento@llnl.gov
 * 	Kathryn Mohror. kathryn@llnl.gov
 * 	LLNL-CODE-728877.
 * All rights reserved.
 *
 * This file is part of BurstFS For details, see https://github.com/llnl/burstfs.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include "mpi.h"
#include "mdhim.h"

#define MANIFEST_FILE_NAME "mdhim_manifest_"
#define GEN_STR_LEN 1024

struct timeval putstart, putend;
struct timeval calstart, calend;
struct timeval putreleasestart, putreleaseend;
double puttime;
double putbw;
double putops;
double caltime;
double putreleasetime = 0;

struct timeval getstart, getend;

double gettime;
double getbw;
double getops;

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

int init_meta_lst(meta_t *meta_lst, ulfs_key_t **key_lst, ulfs_val_t **value_lst, \
		long segnum, long transz, int rank, int size);
void init_get_key_lst(ulfs_key_t **get_key_lst,\
		int *get_key_lens, int *get_val_lens,\
			int key_cnt, long gettransz, int rank, int size);

int main(int argc, char **argv) {
	int c, serratio, bulknum, ret, provided, size, path_len;

	long transz, segnum, rangesz, gettransz;

	char db_path[GEN_STR_LEN] = {0};
	char db_name[GEN_STR_LEN] = {0};

	MPI_Comm comm;

	struct mdhim_t *md;
	struct mdhim_brm_t *brm, *brmp;
	struct mdhim_bgetrm_t *bgrm, *bgrmp;
	mdhim_options_t *db_opts; // Local variable for db create options to be passed

	static const char * opts = "c:t:s:r:n:p:d:g:";

	while((c = getopt(argc, argv, opts)) != -1){
	    switch (c)  {

	      case 'c': /*number of batched key-value pairs in each bput*/
	          bulknum = atoi(optarg); break;
	      case 's': /*server factor same as MDHIM*/
	          serratio = atoi(optarg); break;
	      case 't': /*transfer size*/
	          transz = atol(optarg); break;
	      case 'g':
	    	  gettransz = atol(optarg); break;
	      case 'r': /*the key range for each slice*/
	          rangesz = atol(optarg); break;
	      case 'n': /*number of transfers*/
	          segnum = atol(optarg); break;
	      case 'p': /*path of the database*/
	    	  strcpy(db_path, optarg); break;
	      case 'd': /*name of the database*/
	    	  strcpy(db_name, optarg); break;
	    }
	}

	printf("gettransz:%ld,transz:%ld,num:%d\n", gettransz, transz, segnum);
	fflush(stdout);

	db_opts = malloc(sizeof(struct mdhim_options_t));

	db_opts->db_path = db_path;
	db_opts->db_name = "ulfsDB";
	db_opts->manifest_path = NULL;
	db_opts->db_type = LEVELDB;
	db_opts->db_create_new = 1;
	db_opts->db_value_append = MDHIM_DB_OVERWRITE;

	db_opts->rserver_factor = serratio;
	db_opts->db_paths = NULL;
	db_opts->num_paths = 0;
	db_opts->num_wthreads = 1;

	path_len = strlen(db_opts->db_path) + strlen(MANIFEST_FILE_NAME) + 1;

	char *manifest_path;
	manifest_path = malloc(path_len);
	sprintf(manifest_path, "%s%s", db_opts->db_path, MANIFEST_FILE_NAME);
	db_opts->manifest_path = manifest_path;
	db_opts->db_name = db_name;
	db_opts->db_type = LEVELDB;

	db_opts->db_key_type = MDHIM_BURSTFS_KEY;
	db_opts->debug_level = MLOG_CRIT;
	db_opts->max_recs_per_slice = rangesz;
	db_opts->rserver_factor = serratio;

	ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (ret != MPI_SUCCESS) {
		printf("Error initializing MPI with threads\n");
		exit(1);
	}

	if (provided != MPI_THREAD_MULTIPLE) {
                printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
                exit(1);
    }

	comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);

	if (!md) {
		printf("Error initializing MDHIM\n");
		MPI_Abort(MPI_COMM_WORLD, ret);
		exit(1);
	}

	meta_t *meta_lst = (meta_t *)malloc(segnum*sizeof(meta_t));
	ulfs_key_t **key_lst = (ulfs_key_t **)malloc(segnum*sizeof(ulfs_key_t *));
	ulfs_val_t **val_lst = (ulfs_val_t **)malloc(segnum * sizeof(ulfs_val_t *));

	int *key_lens = (int *)malloc(segnum * sizeof(int));
	int *val_lens = (int *)malloc(segnum * sizeof(int));

	long i;
	for (i = 0; i < segnum; i++) {
		key_lens[i] = sizeof(ulfs_key_t);
		val_lens[i] = sizeof(ulfs_val_t);
	}

	int glb_size, glb_rank;
	MPI_Comm_size(MPI_COMM_WORLD, &glb_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &glb_rank);
	init_meta_lst(meta_lst, key_lst, val_lst, segnum,\
			transz, glb_rank, glb_size);

	MPI_Comm_size(md->mdhim_comm, &size);
	MPI_Barrier(MPI_COMM_WORLD);

	gettimeofday(&putstart, NULL);
	long total_keys = 0, round = 0;

		//Insert the keys into MDHIM
		brm = mdhimBPut(md, (void **)(&key_lst[total_keys]), key_lens,
				(void **) (&val_lst[total_keys]), val_lens, segnum,
				NULL, NULL);
		//Iterate through the return messages to see if there is an error and to free it
		brmp = brm;
		if (!brmp || brmp->error) {
              printf("Rank - %d: Error inserting keys/values into MDHIM\n", md->mdhim_rank);
		}

		gettimeofday(&putreleasestart, NULL);
		while (brmp) {
			if (brmp->error < 0) {
				printf("Rank: %d - Error inserting key/values info MDHIM\n", md->mdhim_rank);
			}

		brm = brmp;
		brmp = brmp->next;
		//Free the message
		mdhim_full_release_msg(brm);

		}
		gettimeofday(&putreleaseend, NULL);
		putreleasetime = 1000000*(putreleaseend.tv_sec-putreleasestart.tv_sec)\
				+ putreleaseend.tv_usec - putreleasestart.tv_usec;

		round++;

	MPI_Barrier(MPI_COMM_WORLD);
	gettimeofday(&putend, NULL);
	puttime = 1000000*(putend.tv_sec - putstart.tv_sec) + putend.tv_usec -putstart.tv_usec;
	puttime/=1000000;
	putbw = (sizeof(ulfs_key_t)+sizeof(ulfs_val_t))*segnum*size/puttime;
	putops = segnum/puttime;
	gettimeofday(&calstart, NULL);
	ret = mdhimStatFlush(md, md->primary_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error getting stats from MDHIM database\n");
	} else {
	}
	gettimeofday(&calend, NULL);
	caltime = 1000000*(calend.tv_sec-calstart.tv_sec)+calend.tv_usec-calstart.tv_usec;
	caltime = caltime/1000000;

	total_keys = 0;

	ulfs_key_t **get_key_lst = (ulfs_key_t **)malloc(2 * segnum\
			* transz / gettransz * sizeof(ulfs_key_t *));
	int *get_key_lens =\
			(long *)malloc(2 * segnum * transz / gettransz * sizeof(int));
	int *get_val_lens = \
			(long *)malloc(2 * segnum * transz / gettransz * sizeof(int));

	init_get_key_lst(get_key_lst, get_key_lens, get_val_lens,\
			segnum * transz / gettransz,\
			gettransz, glb_rank, glb_size);

	gettimeofday(&getstart, NULL);
	//Get the keys and values back starting from and including key[0]

	total_keys = 0;

	bgrm = mdhimBGet(md, md->primary_index,\
	 &get_key_lst[total_keys], get_key_lens,
	 	 segnum * transz / gettransz* 2, MDHIM_RANGE_BGET);
	bgrmp = bgrm;
	while (bgrmp) {
		if (bgrmp->error < 0) {
			printf("Rank: %d - Error retrieving values", md->mdhim_rank);
		}
		for (i = 0; i < bgrmp->num_keys; i++) {
		/*	printf("Rank: %d - Got key: %ld, num_keys is %ld\n", md->mdhim_rank,
				((ulfs_key_t *)bgrmp->keys[i])->offset, bgrmp->num_keys );
			fflush(stdout);
		*/
		}

		bgrmp = bgrmp->next;
		//Free the message received
		mdhim_full_release_msg(bgrm);
		bgrm = bgrmp;
	}

	MPI_Barrier(MPI_COMM_WORLD);
	gettimeofday(&getend, NULL);
	gettime = 1000000*(getend.tv_sec - getstart.tv_sec)+getend.tv_usec-getstart.tv_usec;
	gettime/=1000000;
	getops = segnum/gettime;

	if (md->mdhim_rank == size - 1) {
		printf("puttime is %lf, putops is %lf, rank is %d\n",\
				puttime, putops, md->mdhim_rank); fflush(stdout);
		printf("gettime is %lf, getops is %lf, rank is %d\n",\
				gettime, getops, md->mdhim_rank); fflush(stdout);
	}

	free(meta_lst);

	free(key_lens);
	for (i=0; i<segnum; i++) {
		free(key_lst[i]);
	}
	free(key_lst);

	free(val_lens);
	for (i=0; i<segnum; i++) {
		free(val_lst[i]);
	}
	free(val_lst);

done:
	MPI_Barrier(MPI_COMM_WORLD);
	//Quit MDHIM
	ret = mdhimClose(md);
	mdhim_options_destroy(db_opts);

	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}

/*generate the metadata for N-1 segmented write/read, where each process
 * writes a non-overlapping region of a shared file, can
 * modify the offset, fid to test N-1 strided and N-N write/read based
 * on the commented code inside*/
int init_meta_lst(meta_t *meta_lst, ulfs_key_t **key_lst, ulfs_val_t **value_lst, \
		long segnum, long transz, int rank, int glb_size) {

	long i=0;
	for (i = 0; i < segnum; i++) {
		meta_lst[i].fid = 0;
//		meta_lst[i].fid = rank;

//		meta_lst[i].offset = i * transz + rank * transz * segnum;
//		meta_lst[i].offset = i * transz;
		meta_lst[i].offset = i * glb_size * transz + rank * transz;
		meta_lst[i].nodeid = rank;
		meta_lst[i].addr = i * transz;
		meta_lst[i].len = transz;

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

void init_get_key_lst(ulfs_key_t **get_key_lst,\
		int *get_key_lens, int *get_val_lens,\
			int key_cnt, long gettransz, int rank, int glb_size) {
	long i;
	for (i = 0; i < key_cnt; i++) {
		get_key_lst[2 * i] = (ulfs_key_t *)malloc(sizeof(ulfs_key_t));
		get_key_lst[2 * i]->fid = 0;
//		get_key_lst[2 * i]->fid = rank;
//		get_key_lst[2 * i]->offset = (long)i * gettransz + rank * gettransz * key_cnt;
//		get_key_lst[2 * i]->offset = (long)i * gettransz;
		get_key_lst[2 * i]->offset = (long)i * gettransz * glb_size + rank * gettransz;
		get_key_lens[2 * i] = sizeof(ulfs_key_t);
		get_val_lens[2 * i] = sizeof(ulfs_val_t);

		get_key_lst[2 * i + 1] = (ulfs_key_t *)malloc(sizeof(ulfs_key_t));
		get_key_lst[2 * i + 1]->fid = 0;
		get_key_lst[2 * i + 1]->offset = get_key_lst[2 * i]->offset + gettransz - 1;
		get_key_lens[2 * i + 1] = sizeof(ulfs_key_t);
		get_val_lens[2 * i + 1] = sizeof(ulfs_val_t);


	}
}


