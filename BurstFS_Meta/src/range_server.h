/*
* Copyright (c) 2017, Lawrence Livermore National Security, LLC.
* Produced at the Lawrence Livermore National Laboratory.
* Copyright (c) 2017, Florida State University. Contributions from
* the Computer Architecture and Systems Research Laboratory (CASTL)
* at the Department of Computer Science.
*
* Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
* LLNL-CODE-728877. All rights reserved.
*
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

/*
*
* Copyright (c) 2014, Los Alamos National Laboratory
*	All rights reserved.
*
*/

#ifndef      __RANGESRV_H
#define      __RANGESRV_H

#include <pthread.h>
#include <mpi.h>
#include "data_store.h"
#include "messages.h"
#include "indexes.h"

struct mdhim_t;

typedef struct work_item work_item;
struct work_item {
	work_item *next;
	work_item *prev;
	void *message;
	int source;
};

typedef struct work_queue_t {
	work_item *head;
	work_item *tail;
} work_queue_t;

/* Outstanding requests (i.e., MPI_Req) that need to be freed later */
typedef struct out_req out_req;
struct out_req {
	out_req *next;
	out_req *prev;
	void *req;
	MPI_Request *message;
};

/* Range server specific data */
typedef struct mdhim_rs_t {
	work_queue_t *work_queue;
	pthread_mutex_t *work_queue_mutex;
	pthread_cond_t *work_ready_cv;
	pthread_t listener;
	pthread_t **workers;
	struct index *indexes; /* A linked list of remote indexes that is served 
				  (partially for fully) by this range server */
	//Records seconds spent on putting records
	long double put_time; 
	//Records seconds spend on getting records
	long double get_time;
	long num_put;
	long num_get;
	out_req *out_req_list;
	pthread_mutex_t *out_req_mutex;
} mdhim_rs_t;

int range_server_add_work(struct mdhim_t *md, work_item *item);
int range_server_init(struct mdhim_t *md);
int range_server_init_comm(struct mdhim_t *md);
int range_server_stop(struct mdhim_t *md);
int range_server_add_oreq(struct mdhim_t *md, MPI_Request *req, void *msg); //Add an outstanding request
int range_server_clean_oreqs(struct mdhim_t *md); //Clean outstanding reqs
int levedb_batch_ranges(void *dbh, char **key, int *key_len,\
		char ***out_key, int **out_key_len,\
			char ***out_val, int **out_val_len,\
				int tot_records, int *out_records_cnt);

#endif
