/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

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
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *	All rights reserved.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ds_leveldb.h"
#include "mdhim.h"
#include "mdhim_options.h"
#include "partitioner.h"
#include "range_server.h"
#include "unifyfs_metadata_mdhim.h"
#include "uthash.h"

int recv_counter = 0;

struct timeval resp_put_comm_start, resp_put_comm_end;
double resp_put_comm_time = 0;

struct timeval resp_get_comm_start, resp_get_comm_end;
double resp_get_comm_time = 0;
struct index_t *tmp_index;

struct timeval worker_start, worker_end;
double worker_time=0;

struct timeval worker_get_start, worker_get_end;
double worker_get_time=0;

struct timeval worker_put_start, worker_put_end;
double worker_put_time=0;

struct timeval stat_start, stat_end;
double stat_time=0;

struct timeval odbgetstart, odbgetend;
double odbgettime=0;

struct timeval bputstart, bputend;
double bputtime=0;

struct timeval statstart, statend;
double starttime=0;

int putflag = 1;

int unifyfs_compare(const char* a, const char* b) {
	int rc;
	unifyfs_key_t *keya = (unifyfs_key_t *)a;
	unifyfs_key_t *keyb = (unifyfs_key_t *)b;
	rc = unifyfs_key_compare(keya, keyb);
	return rc;
}

void add_timing(struct timeval start, struct timeval end, int num, 
		struct mdhim_t *md, int mtype) {
	long double elapsed;

	elapsed = (long double) (end.tv_sec - start.tv_sec) + 
		((long double) (end.tv_usec - start.tv_usec)/1000000.0);
	if (mtype == MDHIM_PUT || mtype == MDHIM_BULK_PUT) {
		md->mdhim_rs->put_time += elapsed;
		md->mdhim_rs->num_put += num;
	} else if (mtype == MDHIM_BULK_GET) {
		md->mdhim_rs->get_time += elapsed;
		md->mdhim_rs->num_get += num;
	}
}

/**
 * send_locally_or_remote
 * Sends the message remotely or locally
 *
 * @param md       Pointer to the main MDHIM structure
 * @param dest     Destination rank
 * @param message  pointer to message to send
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int send_locally_or_remote(struct mdhim_t *md, int dest, void *message) {
	int ret = MDHIM_SUCCESS;
	MPI_Request **size_req, **msg_req;
	int *sizebuf;
	void **sendbuf;

	if (md->mdhim_rank != dest) {
		//Sends the message remotely
		size_req = malloc(sizeof(MPI_Request *));
		msg_req = malloc(sizeof(MPI_Request *));
		sendbuf = malloc(sizeof(void *));
		sizebuf = malloc(sizeof(int));
		ret = send_client_response(md, dest, message, sizebuf, 
					   sendbuf, size_req, msg_req);

		if (*size_req) {
			range_server_add_oreq(md, *size_req, sizebuf);
		} else {
		  free(sizebuf);
		}

		if (*msg_req) {
			range_server_add_oreq(md, *msg_req, *sendbuf);
		} else if (*sendbuf) {
			free(*sendbuf);
		}
		
		free(sendbuf);
		mdhim_full_release_msg(message);
		free(size_req);
		free(msg_req);
	} else {
		//Sends the message locally
		pthread_mutex_lock(md->receive_msg_mutex);
		md->receive_msg = message;
		pthread_mutex_unlock(md->receive_msg_mutex);
		pthread_cond_signal(md->receive_msg_ready_cv);
	}

	return ret;
}

struct index_t *find_index(struct mdhim_t *md, struct mdhim_basem_t *msg) {
	struct index_t *ret;
       
	ret = get_index(md, msg->index);

	return ret;

}


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  find_index_by_name
 *  Description:  Search for index by name
 *    Variables:  <struct mdhim_t *md> the pointer to the mdhim structure
 *                <struct mdhim_basem_t *msg> A pointer to a base message that contains
 *                                            the name of the index
 * =====================================================================================
 */
struct index_t * find_index_by_name(struct mdhim_t *md, struct mdhim_basem_t *msg) {
    struct index_t *ret;

    ret = get_index_by_name(md, msg->index_name);

    return ret;
}

/**
 * range_server_add_work
 * Adds work to the work queue and signals the condition variable for the worker thread
 *
 * @param md      Pointer to the main MDHIM structure
 * @param item    pointer to new work item that contains a message to handle
 * @return MDHIM_SUCCESS
 */
int range_server_add_work(struct mdhim_t *md, work_item *item) {
	//Lock the work queue mutex
	pthread_mutex_lock(md->mdhim_rs->work_queue_mutex);
	item->next = NULL;
	item->prev = NULL;       
	
	//Add work to the tail of the work queue
	if (md->mdhim_rs->work_queue->tail) {
		md->mdhim_rs->work_queue->tail->next = item;
		item->prev = md->mdhim_rs->work_queue->tail;
		md->mdhim_rs->work_queue->tail = item;
	} else {
		md->mdhim_rs->work_queue->head = item;
		md->mdhim_rs->work_queue->tail = item;
	}

	//Signal the waiting thread that there is work available
	pthread_mutex_unlock(md->mdhim_rs->work_queue_mutex);
	pthread_cond_signal(md->mdhim_rs->work_ready_cv);

	return MDHIM_SUCCESS;
}

/**
 * get_work
 * Returns the next work item from the work queue
 *
 * @param md  Pointer to the main MDHIM structure
 * @return  the next work_item to process
 */

work_item *get_work(struct mdhim_t *md) {
	work_item *item;

	item = md->mdhim_rs->work_queue->head;
	if (!item) {
		return NULL;
	}

	//Set the list head and tail to NULL
	md->mdhim_rs->work_queue->head = NULL;
	md->mdhim_rs->work_queue->tail = NULL;

	//Return the list
	return item;
}

/**
 * range_server_stop
 * Stop the range server (i.e., stops the threads and frees the relevant data in md)
 *
 * @param md  Pointer to the main MDHIM structure
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_stop(struct mdhim_t *md) {
	int i, ret;
	work_item *head, *temp_item;

	//Signal to the listener thread that it needs to shutdown
	md->shutdown = 1;

	/* Wait for the threads to finish */
	pthread_cond_broadcast(md->mdhim_rs->work_ready_cv);
	pthread_join(md->mdhim_rs->listener, NULL);
	/* Wait for the threads to finish */
	for (i = 0; i < md->db_opts->num_wthreads; i++) {
		pthread_join(*md->mdhim_rs->workers[i], NULL);
		free(md->mdhim_rs->workers[i]);
	}
	free(md->mdhim_rs->workers);
		
	//Destroy the condition variables
	if ((ret = pthread_cond_destroy(md->mdhim_rs->work_ready_cv)) != 0) {
	  mlog(MDHIM_SERVER_DBG, "Rank: %d - Error destroying work cond variable", 
	       md->mdhim_rank);
	}
	free(md->mdhim_rs->work_ready_cv);
		
	//Destroy the work queue mutex
	if ((ret = pthread_mutex_destroy(md->mdhim_rs->work_queue_mutex)) != 0) {
	  mlog(MDHIM_SERVER_DBG, "Rank: %d - Error destroying work queue mutex", 
	       md->mdhim_rank);
	}
	free(md->mdhim_rs->work_queue_mutex);
		
	//Clean outstanding sends
	range_server_clean_oreqs(md);
	//Destroy the out req mutex
	if ((ret = pthread_mutex_destroy(md->mdhim_rs->out_req_mutex)) != 0) {
	  mlog(MDHIM_SERVER_DBG, "Rank: %d - Error destroying work queue mutex", 
	       md->mdhim_rank);
	}
	free(md->mdhim_rs->out_req_mutex);
		
	//Free the work queue
	head = md->mdhim_rs->work_queue->head;
	while (head) {
	  temp_item = head->next;
	  free(head);
	  head = temp_item;
	}
	free(md->mdhim_rs->work_queue);
		
	mlog(MDHIM_SERVER_INFO, "Rank: %d - Inserted: %ld records in %Lf seconds", 
	     md->mdhim_rank, md->mdhim_rs->num_put, md->mdhim_rs->put_time);
	mlog(MDHIM_SERVER_INFO, "Rank: %d - Retrieved: %ld records in %Lf seconds", 
	     md->mdhim_rank, md->mdhim_rs->num_get, md->mdhim_rs->get_time);
	  
	//Free the range server data
	free(md->mdhim_rs);
	md->mdhim_rs = NULL;

	return MDHIM_SUCCESS;
}

/**
 * range_server_put
 * Handles the put message and puts data in the database
 *
 * @param md        pointer to the main MDHIM struct
 * @param im        pointer to the put message to handle
 * @param source    source of the message
 * @return          MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_put(struct mdhim_t *md, struct mdhim_putm_t *im, int source) {
	int ret;
	struct mdhim_rm_t *rm;
	int error = 0;
	void **value;
	int32_t *value_len;
	int exists = 0;
	void *new_value;
	int32_t new_value_len;
	void *old_value;
	int32_t old_value_len;
	struct timeval start, end;
	int inserted = 0;
	struct index_t *index;

	value = malloc(sizeof(void *));
	*value = NULL;
	value_len = malloc(sizeof(int32_t));
	*value_len = 0;

	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) im);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, im->basem.index);
		error = MDHIM_ERROR;
		goto done;
	}

	gettimeofday(&start, NULL);
       //Check for the key's existence
/*	index->mdhim_store->get(index->mdhim_store->db_handle, 
				       im->key, im->key_len, value, 
				       value_len);
*/
	//The key already exists
	if (*value && *value_len) {
		exists = 1;
	}

        //If the option to append was specified and there is old data, concat the old and new
	if (exists &&  md->db_opts->db_value_append == MDHIM_DB_APPEND) {
		old_value = *value;
		old_value_len = *value_len;
		new_value_len = old_value_len + im->value_len;
		new_value = malloc(new_value_len);
		memcpy(new_value, old_value, old_value_len);
		memcpy(new_value + old_value_len, im->value, im->value_len);
	} else {
		new_value = im->value;
		new_value_len = im->value_len;
	}
    
	if (*value && *value_len) {
		free(*value);
	}
	free(value);
	free(value_len);
        //Put the record in the database
	if ((ret = 
	     index->mdhim_store->put(index->mdhim_store->db_handle, 
				     im->key, im->key_len, new_value, 
				     new_value_len)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error putting record", 
		     md->mdhim_rank);	
		error = ret;
	} else {
		inserted = 1;
	}

	if (!exists && error == MDHIM_SUCCESS) {
		gettimeofday(&stat_start, NULL);
		update_stat(md, index, im->key, im->key_len);
		gettimeofday(&stat_end, NULL);
		stat_time += 1000000 * (stat_end.tv_sec - stat_start.tv_sec) + \
			stat_end.tv_usec - stat_start.tv_usec;
	}

	gettimeofday(&end, NULL);
	add_timing(start, end, inserted, md, MDHIM_PUT);

done:
	//Create the response message
	rm = malloc(sizeof(struct mdhim_rm_t));
	//Set the type
	rm->basem.mtype = MDHIM_RECV;
	//Set the operation return code as the error
	rm->error = error;
	//Set the server's rank
	rm->basem.server_rank = md->mdhim_rank;
	
	//Send response
	ret = send_locally_or_remote(md, source, rm);

	//Free memory
	if (exists && md->db_opts->db_value_append == MDHIM_DB_APPEND) {
		free(new_value);
	}
	if (source != md->mdhim_rank) {
		free(im->key);
		free(im->value);
	} 
	free(im);
	return MDHIM_SUCCESS;
}


/**
 * range_server_bput
 * Handles the bulk put message and puts data in the database
 *
 * @param md        Pointer to the main MDHIM struct
 * @param bim       pointer to the bulk put message to handle
 * @param source    source of the message
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_bput(struct mdhim_t *md, struct mdhim_bputm_t *bim, int source) {
	putflag = 1;
	int i;
	int ret;
	int error = MDHIM_SUCCESS;
	struct mdhim_rm_t *brm;
	void **value;
	int32_t *value_len;
	int *exists;
	void *new_value;
	int32_t new_value_len;
	void **new_values;
	int32_t *new_value_lens;
	void *old_value;
	int32_t old_value_len;
	struct timeval start, end;
	int num_put = 0;
	struct index_t *index;

	gettimeofday(&start, NULL);
	gettimeofday(&bputstart, NULL);
	exists = malloc(bim->num_keys * sizeof(int));
	new_values = malloc(bim->num_keys * sizeof(void *));
	new_value_lens = malloc(bim->num_keys * sizeof(int));
	value = malloc(sizeof(void *));
	value_len = malloc(sizeof(int32_t));

	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) bim);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, bim->basem.index);
		error = MDHIM_ERROR;
		goto done;
	}
	gettimeofday(&bputend, NULL);
	bputtime+=1000000 * (bputend.tv_sec - bputstart.tv_sec)\
			+ bputend.tv_usec - bputstart.tv_usec;

	for (i = 0; i < bim->num_keys && i < MAX_BULK_OPS; i++) {	
		*value = NULL;
		*value_len = 0;

		gettimeofday(&odbgetstart, NULL);
/*		index->mdhim_store->get(index->mdhim_store->db_handle, 
					       bim->keys[i], bim->key_lens[i], value, 
					       value_len);
*/
		if (*value && *value_len) {
			exists[i] = 1;
		} else {
			exists[i] = 0;
		}

		exists[i] = 0;
		if (exists[i] && md->db_opts->db_value_append == MDHIM_DB_APPEND) {
			old_value = *value;
			old_value_len = *value_len;
			new_value_len = old_value_len + bim->value_lens[i];
			new_value = malloc(new_value_len);
			memcpy(new_value, old_value, old_value_len);
			memcpy(new_value + old_value_len, bim->values[i], bim->value_lens[i]);		
			if (exists[i] && source != md->mdhim_rank) {
				free(bim->values[i]);
			}

			new_values[i] = new_value;
			new_value_lens[i] = new_value_len;
		} else {
			new_values[i] = bim->values[i];
			new_value_lens[i] = bim->value_lens[i];

		}
		
		if (*value) {
			free(*value);
		}	
		gettimeofday(&odbgetend, NULL);
		odbgettime+=1000000 * (odbgetend.tv_sec\
				- odbgetstart.tv_sec) + odbgetend.tv_usec - odbgetstart.tv_usec;
	}

	//Put the record in the database
	if ((ret = 
	     index->mdhim_store->batch_put(index->mdhim_store->db_handle, 
					   bim->keys, bim->key_lens, new_values, 
					   new_value_lens, bim->num_keys)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error batch putting records", 
		     md->mdhim_rank);
		error = ret;
	} else {
		num_put = bim->num_keys;
	}

	gettimeofday(&stat_start, NULL);
	for (i = 0; i < bim->num_keys && i < MAX_BULK_OPS; i++) {
		//Update the stats if this key didn't exist before
		if (!exists[i] && error == MDHIM_SUCCESS) {
			update_stat(md, index, bim->keys[i], bim->key_lens[i]);
		}
	       
		if (exists[i] && md->db_opts->db_value_append == MDHIM_DB_APPEND) {
			//Release the value created for appending the new and old value
			free(new_values[i]);
		}		

		//Release the bput keys/value if the message isn't coming from myself
		if (source != md->mdhim_rank) {
			free(bim->keys[i]);
			free(bim->values[i]);
		} 
	}
	gettimeofday(&stat_end, NULL);
	stat_time += 1000000 * (stat_end.tv_sec - stat_start.tv_sec) + \
		stat_end.tv_usec - stat_start.tv_usec;

	free(exists);
	free(new_values);
	free(new_value_lens);
	free(value);
	free(value_len);
	gettimeofday(&end, NULL);
	add_timing(start, end, num_put, md, MDHIM_BULK_PUT);

 done:
	//Create the response message
	brm = malloc(sizeof(struct mdhim_rm_t));
	//Set the type
	brm->basem.mtype = MDHIM_RECV;
	//Set the operation return code as the error
	brm->error = error;
	//Set the server's rank
	brm->basem.server_rank = md->mdhim_rank;

	//Release the internals of the bput message
	free(bim->keys);
	free(bim->key_lens);
	free(bim->values);
	free(bim->value_lens);
	free(bim);

	//Send response
	gettimeofday(&resp_put_comm_start, NULL);	
	ret = send_locally_or_remote(md, source, brm);

	return MDHIM_SUCCESS;
}

/**
 * range_server_del
 * Handles the delete message and deletes the data from the database
 *
 * @param md       Pointer to the main MDHIM struct
 * @param dm       pointer to the delete message to handle
 * @param source   source of the message
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_del(struct mdhim_t *md, struct mdhim_delm_t *dm, int source) {
	int ret = MDHIM_ERROR;
	struct mdhim_rm_t *rm;
	struct index_t *index;

	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) dm);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, dm->basem.index);
		ret = MDHIM_ERROR;
		goto done;
	}

	//Put the record in the database
	if ((ret = 
	     index->mdhim_store->del(index->mdhim_store->db_handle, 
				     dm->key, dm->key_len)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error deleting record", 
		     md->mdhim_rank);
	}

 done:
	//Create the response message
	rm = malloc(sizeof(struct mdhim_rm_t));
	//Set the type
	rm->basem.mtype = MDHIM_RECV;
	//Set the operation return code as the error
	rm->error = ret;
	//Set the server's rank
	rm->basem.server_rank = md->mdhim_rank;

	//Send response
	ret = send_locally_or_remote(md, source, rm);
	free(dm);

	return MDHIM_SUCCESS;
}

/**
 * range_server_bdel
 * Handles the bulk delete message and deletes the data from the database
 *
 * @param md        Pointer to the main MDHIM struct
 * @param bdm       pointer to the bulk delete message to handle
 * @param source    source of the message
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_bdel(struct mdhim_t *md, struct mdhim_bdelm_t *bdm, int source) {
 	int i;
	int ret;
	int error = 0;
	struct mdhim_rm_t *brm;
	struct index_t *index;

	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) bdm);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, bdm->basem.index);
		error = MDHIM_ERROR;
		goto done;
	}

	//Iterate through the arrays and delete each record
	for (i = 0; i < bdm->num_keys && i < MAX_BULK_OPS; i++) {
		//Put the record in the database
		if ((ret = 
		     index->mdhim_store->del(index->mdhim_store->db_handle, 
					     bdm->keys[i], bdm->key_lens[i])) 
		    != MDHIM_SUCCESS) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error deleting record", 
			     md->mdhim_rank);
			error = ret;
		}
	}

done:
	//Create the response message
	brm = malloc(sizeof(struct mdhim_rm_t));
	//Set the type
	brm->basem.mtype = MDHIM_RECV;
	//Set the operation return code as the error
	brm->error = error;
	//Set the server's rank
	brm->basem.server_rank = md->mdhim_rank;

	//Send response
	ret = send_locally_or_remote(md, source, brm);
	free(bdm->keys);
	free(bdm->key_lens);
	free(bdm);

	return MDHIM_SUCCESS;
}

/**
 * range_server_commit
 * Handles the commit message and commits outstanding writes to the database
 *
 * @param md        pointer to the main MDHIM struct
 * @param im        pointer to the commit message to handle
 * @param source    source of the message
 * @return          MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_commit(struct mdhim_t *md, struct mdhim_basem_t *im, int source) {
	int ret;
	struct mdhim_rm_t *rm;
	struct index_t *index;

	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) im);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, im->index);
		ret = MDHIM_ERROR;
		goto done;
	}

        //Put the record in the database
	if ((ret = 
	     index->mdhim_store->commit(index->mdhim_store->db_handle)) 
	    != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error committing database", 
		     md->mdhim_rank);	
	}

 done:	
	//Create the response message
	rm = malloc(sizeof(struct mdhim_rm_t));
	//Set the type
	rm->basem.mtype = MDHIM_RECV;
	//Set the operation return code as the error
	rm->error = ret;
	//Set the server's rank
	rm->basem.server_rank = md->mdhim_rank;

	//Send response
	ret = send_locally_or_remote(md, source, rm);
	free(im);

	return MDHIM_SUCCESS;
}

/**
 * range_server_bget
 * Handles the bulk get message, retrieves the data from the database, and sends the results back
 * 
 * @param md        Pointer to the main MDHIM struct
 * @param bgm       pointer to the bulk get message to handle
 * @param source    source of the message
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_bget(struct mdhim_t *md, struct mdhim_bgetm_t *bgm, int source) {
	putflag = 0;
	int ret;
	void **values = NULL;
	int32_t *value_lens = NULL;
	int i;
	struct mdhim_bgetrm_t *bgrm;
	int error = 0;
	struct timeval start, end;
	int num_retrieved = 0;
	struct index_t *index;

	gettimeofday(&start, NULL);
	if (bgm->op != MDHIM_RANGE_BGET) {
		values = (void **) calloc(bgm->num_keys, sizeof(void *));
		value_lens = (int32_t *) calloc(bgm->num_keys, sizeof(int32_t));
	}
	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) bgm);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, bgm->basem.index);
		error = MDHIM_ERROR;
		goto done;
	}

	if (bgm->op == MDHIM_RANGE_BGET) {
		void **ret_keys;
		int32_t *ret_key_lens;
		int num_ranges = bgm->num_keys / 2;
		int out_record_cnt = 0;
		leveldb_batch_ranges(index->mdhim_store->db_handle,
                                    (char **)bgm->keys, bgm->key_lens,
                                    (char ***)&ret_keys, &ret_key_lens,
                                    (char ***)&values, &value_lens,
                                    num_ranges, &out_record_cnt);

		if (source != md->mdhim_rank) {
			for (i = 0; i < bgm->num_keys; i++) {
				free(bgm->keys[i]);
			}
		}
		free(bgm->key_lens);
		free(bgm->keys);

		bgm->keys = ret_keys;
		bgm->num_keys = out_record_cnt;
		bgm->key_lens = ret_key_lens;

	} else {
		for (i = 0; i < bgm->num_keys && i < MAX_BULK_OPS; i++) {
			switch(bgm->op) {
				// Gets the value for the given key
			case MDHIM_GET_EQ:
				//Get records from the database

				if ((ret =
					 index->mdhim_store->get(index->mdhim_store->db_handle,
								 bgm->keys[i], bgm->key_lens[i], &values[i],
								 &value_lens[i])) != MDHIM_SUCCESS) {
					error = ret;
					value_lens[i] = 0;
					values[i] = NULL;
					continue;
				}

				break;
				/* Gets the next key and value that is in order after the passed in key */
			case MDHIM_GET_NEXT:
				if ((ret =
					 index->mdhim_store->get_next(index->mdhim_store->db_handle,
								  &bgm->keys[i], &bgm->key_lens[i], &values[i],
								  &value_lens[i])) != MDHIM_SUCCESS) {
					mlog(MDHIM_SERVER_DBG, "Rank: %d - Error getting record", md->mdhim_rank);
					error = ret;
					value_lens[i] = 0;
					values[i] = NULL;
					continue;
				}

				break;
				/* Gets the previous key and value that is in order before the passed in key
				   or the last key if no key was passed in */
			case MDHIM_GET_PREV:
				if ((ret =
					 index->mdhim_store->get_prev(index->mdhim_store->db_handle,
								  &bgm->keys[i], &bgm->key_lens[i], &values[i],
								  &value_lens[i])) != MDHIM_SUCCESS) {
					mlog(MDHIM_SERVER_DBG, "Rank: %d - Error getting record", md->mdhim_rank);
					error = ret;
					value_lens[i] = 0;
					values[i] = NULL;
					continue;
				}

				break;
				/* Gets the first key/value */
			case MDHIM_GET_FIRST:
				if ((ret =
					 index->mdhim_store->get_next(index->mdhim_store->db_handle,
								  &bgm->keys[i], 0, &values[i],
								  &value_lens[i])) != MDHIM_SUCCESS) {
					mlog(MDHIM_SERVER_DBG, "Rank: %d - Error getting record", md->mdhim_rank);
					error = ret;
					value_lens[i] = 0;
					values[i] = NULL;
					continue;
				}

				break;
				/* Gets the last key/value */
			case MDHIM_GET_LAST:
				if ((ret =
					 index->mdhim_store->get_prev(index->mdhim_store->db_handle,
								  &bgm->keys[i], 0, &values[i],
								  &value_lens[i])) != MDHIM_SUCCESS) {
					mlog(MDHIM_SERVER_DBG, "Rank: %d - Error getting record", md->mdhim_rank);
					error = ret;
					value_lens[i] = 0;
					values[i] = NULL;
					continue;
				}

				break;
			default:
				mlog(MDHIM_SERVER_DBG, "Rank: %d - Invalid operation: %d given in range_server_get",
					 md->mdhim_rank, bgm->op);
				continue;
			}

			num_retrieved++;
		}
	}
	gettimeofday(&end, NULL);
	add_timing(start, end, num_retrieved, md, MDHIM_BULK_GET);

done:
	//Create the response message
	bgrm = malloc(sizeof(struct mdhim_bgetrm_t));
	//Set the type
	bgrm->basem.mtype = MDHIM_RECV_BULK_GET;
	//Set the operation return code as the error
	bgrm->error = error;
	//Set the server's rank
	bgrm->basem.server_rank = md->mdhim_rank;
	//Set the key and value
	if (source == md->mdhim_rank) {
		//If this message is coming from myself, copy the keys
		bgrm->key_lens = malloc(bgm->num_keys * sizeof(int));		
		bgrm->keys = malloc(bgm->num_keys * sizeof(void *));
		for (i = 0; i < bgm->num_keys; i++) {
			bgrm->key_lens[i] = bgm->key_lens[i];
			bgrm->keys[i] = malloc(bgrm->key_lens[i]);
			memcpy(bgrm->keys[i], bgm->keys[i], bgrm->key_lens[i]);
		}

		free(bgm->keys);
		free(bgm->key_lens);
	} else {
		bgrm->keys = bgm->keys;
		bgrm->key_lens = bgm->key_lens;
	}

	bgrm->values = values;
	bgrm->value_lens = value_lens;
	bgrm->num_keys = bgm->num_keys;
	bgrm->basem.index = index->id;
	bgrm->basem.index_type = index->type;

	//Send response
	gettimeofday(&resp_get_comm_start, NULL);
	ret = send_locally_or_remote(md, source, bgrm);

	//Release the bget message
	free(bgm);

	return MDHIM_SUCCESS;
}

/**
 * range_server_bget_op
 * Handles the get message given an op and number of records greater than 1
 * 
 * @param md        Pointer to the main MDHIM struct
 * @param gm        pointer to the get message to handle
 * @param source    source of the message
 * @param op        operation to perform
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_bget_op(struct mdhim_t *md, struct mdhim_bgetm_t *bgm, int source, int op) {
	putflag = 0;
	int error = 0;
	void **values;
	void **keys;
	void **get_key; //Used for passing the key to the db
	int *get_key_len; //Used for passing the key len to the db
	void **get_value;
	int *get_value_len;
	int32_t *key_lens;
	int32_t *value_lens;
	struct mdhim_bgetrm_t *bgrm;
	int ret;
	int i, j;
	int num_records;
	struct timeval start, end;
	struct index_t *index;

	//Initialize pointers and lengths
	values = malloc(sizeof(void *) * bgm->num_keys * bgm->num_recs);
	value_lens = malloc(sizeof(int32_t) * bgm->num_keys * bgm->num_recs);
	memset(value_lens, 0, sizeof(int32_t) *bgm->num_keys * bgm->num_recs);
	keys = malloc(sizeof(void *) * bgm->num_keys * bgm->num_recs);
	memset(keys, 0, sizeof(void *) * bgm->num_keys * bgm->num_recs);
	key_lens = malloc(sizeof(int32_t) * bgm->num_keys * bgm->num_recs);
	memset(key_lens, 0, sizeof(int32_t) * bgm->num_keys * bgm->num_recs);
	get_key = malloc(sizeof(void *));
	*get_key = NULL;
	get_key_len = malloc(sizeof(int32_t));
	*get_key_len = 0;
	get_value = malloc(sizeof(void *));
	get_value_len = malloc(sizeof(int32_t));
	num_records = 0;
	/*
	printf("range server bget op\n");
	fflush(stdout);
	*/
	//Get the index referenced the message
	index = find_index(md, (struct mdhim_basem_t *) bgm);
	if (!index) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error retrieving index for id: %d", 
		     md->mdhim_rank, bgm->basem.index);
		error = MDHIM_ERROR;
		goto respond;
	}

	if (bgm->num_keys * bgm->num_recs > MAX_BULK_OPS) {
		/*
		printf("in range server, total bulk key%ld, recs %ld\n", bgm->num_keys, bgm->num_recs);
		fflush(stdout);
		*/
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Too many bulk operations requested", 
		     md->mdhim_rank);
		error = MDHIM_ERROR;
		goto respond;
	}

//	mlog(MDHIM_SERVER_CRIT, "Rank: %d - Num keys is: %d and num recs is: %d", 
//	     md->mdhim_rank, bgm->num_keys, bgm->num_recs);

	gettimeofday(&start, NULL);
	//Iterate through the arrays and get each record
	if (op != MDHIM_GET_NEXT) {
		for (i = 0; i < bgm->num_keys; i++) {
			for (j = 0; j < bgm->num_recs; j++) {
				keys[num_records] = NULL;
				key_lens[num_records] = 0;

				//If we were passed in a key, copy it
				if (!j && bgm->key_lens[i] && bgm->keys[i]) {
					*get_key = malloc(bgm->key_lens[i]);
					memcpy(*get_key, bgm->keys[i], bgm->key_lens[i]);
					*get_key_len = bgm->key_lens[i];
					//If we were not passed a key and this is a next/prev, then return an error
				} else if (!j && (!bgm->key_lens[i] || !bgm->keys[i])
					   && (op ==  MDHIM_GET_NEXT ||
						   op == MDHIM_GET_PREV)) {
					error = MDHIM_ERROR;
					goto respond;
				}

				switch(op) {
					//Get a record from the database
				case MDHIM_GET_FIRST:
					if (j == 0) {
						keys[num_records] = NULL;
						key_lens[num_records] = sizeof(int32_t);
					}
				case MDHIM_GET_NEXT:

					if (j && (ret =
						  index->mdhim_store->get_next(index->mdhim_store->db_handle,
										   get_key, get_key_len,
										   get_value,
										   get_value_len))
						!= MDHIM_SUCCESS) {
						mlog(MDHIM_SERVER_DBG, "Rank: %d - Couldn't get next record",
							 md->mdhim_rank);
						error = ret;

						key_lens[num_records] = 0;
						value_lens[num_records] = 0;
						goto respond;
					} else if (!j && (ret =
							  index->mdhim_store->get(index->mdhim_store->db_handle,
										  *get_key, *get_key_len,
										  get_value,
										  get_value_len))
						   != MDHIM_SUCCESS) {
					 if ((ret = index->mdhim_store->get_next(index->mdhim_store->db_handle,\
										  get_key, get_key_len, get_value, \
												get_value_len)) != MDHIM_SUCCESS) {

						 key_lens[num_records] = 0;
						 value_lens[num_records] = 0;
						 goto respond;
						}
					}

					break;
				case MDHIM_GET_LAST:
					if (j == 0) {
						keys[num_records] = NULL;
						key_lens[num_records] = sizeof(int32_t);
					}
				case MDHIM_GET_PREV:
					if (j && (ret =
						  index->mdhim_store->get_prev(index->mdhim_store->db_handle,
										   get_key, get_key_len,
										   get_value,
										   get_value_len))
						!= MDHIM_SUCCESS) {
						mlog(MDHIM_SERVER_DBG, "Rank: %d - Couldn't get prev record",
							 md->mdhim_rank);
						error = ret;
						key_lens[num_records] = 0;
						value_lens[num_records] = 0;
						goto respond;
					} else if (!j && (ret =
							  index->mdhim_store->get(index->mdhim_store->db_handle,
										  *get_key, *get_key_len,
										  get_value,
										  get_value_len))
						   != MDHIM_SUCCESS) {
						error = ret;
						key_lens[num_records] = 0;
						value_lens[num_records] = 0;
						goto respond;
					}
					break;
				default:
					mlog(MDHIM_SERVER_CRIT, "Rank: %d - Invalid operation for bulk get op",
						 md->mdhim_rank);
					goto respond;
					break;
				}

				keys[num_records] = *get_key;
				key_lens[num_records] = *get_key_len;
				values[num_records] = *get_value;
				value_lens[num_records] = *get_value_len;
				num_records++;
			}
		}
	}
	else {
		for (i = 0; i < bgm->num_keys; i++) {
			for (j = 0; j < bgm->num_recs; j++) {
				keys[i*bgm->num_recs+j] = NULL;
				key_lens[i*bgm->num_recs+j] = 0;
			}
		}

		num_records = 0;
		*get_key = malloc(bgm->key_lens[0]);
		memcpy(*get_key, bgm->keys[0], bgm->key_lens[0]);
		keys[0] = *get_key;
		*get_key_len = bgm->key_lens[0];
		key_lens[0] = *get_key_len;

		error = mdhim_leveldb_batch_next(index->mdhim_store->db_handle,
                                                 (char **)keys, key_lens,
                                                 (char **)values, value_lens,
                                                 bgm->num_keys * bgm->num_recs,
                                                 &num_records);

	}

respond:

	gettimeofday(&end, NULL);
	add_timing(start, end, num_records, md, MDHIM_BULK_GET);

	//Create the response message
	bgrm = malloc(sizeof(struct mdhim_bgetrm_t));
	//Set the type
	bgrm->basem.mtype = MDHIM_RECV_BULK_GET;
	//Set the operation return code as the error
	bgrm->error = error;
	//Set the server's rank
	bgrm->basem.server_rank = md->mdhim_rank;
	//Set the keys and values
	bgrm->keys = keys;
	bgrm->key_lens = key_lens;
	bgrm->values = values;
	bgrm->value_lens = value_lens;
	bgrm->num_keys = num_records;
	bgrm->basem.index = index->id;
	bgrm->basem.index_type = index->type;
       
	//Send response
	gettimeofday(&resp_get_comm_start, NULL);
	ret = send_locally_or_remote(md, source, bgrm);
	//Free stuff
	if (source == md->mdhim_rank) {
		/* If this message is not coming from myself, 
		   free the keys and values from the get message */
		mdhim_partial_release_msg(bgm);
	} 

	free(get_key);
	free(get_key_len);
	free(get_value);
	free(get_value_len);

	return MDHIM_SUCCESS;
}

/*
 * listener_thread
 * Function for the thread that listens for new messages
 */
void *listener_thread(void *data) {	
	//Mlog statements could cause a deadlock on range_server_stop due to canceling of threads
	

	struct mdhim_t *md = (struct mdhim_t *) data;
	void *message;
	int source; //The source of the message
	int ret;
	work_item *item;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	while (1) {
		if (md->shutdown) {
			break;
		}	

		//Clean outstanding sends
		range_server_clean_oreqs(md);

		//Receive messages sent to this server
		ret = receive_rangesrv_work(md, &source, &message);
		if (ret < MDHIM_SUCCESS) {		
			continue;
		}
		//printf("Rank: %d - Received message from rank: %d of type: %d", 
		//     md->mdhim_rank, source, mtype);
		recv_counter++;
        //Create a new work item
		item = malloc(sizeof(work_item));
		memset(item, 0, sizeof(work_item));
		             
		//Set the new buffer to the new item's message
		item->message = message;
		//Set the source in the work item
		item->source = source;
		//Add the new item to the work queue
		range_server_add_work(md, item);
	}

	return NULL;
}

/*
 * worker_thread
 * Function for the thread that processes work in work queue
 */
void *worker_thread(void *data) {
	//Mlog statements could cause a deadlock on range_server_stop due to canceling of threads
	struct mdhim_t *md = (struct mdhim_t *) data;
	work_item *item, *item_tmp;
	int mtype;
	int op, num_records, num_keys;

	while (1) {
		if (md->shutdown) {
			break;
		}
		//Lock the work queue mutex
		pthread_mutex_lock(md->mdhim_rs->work_queue_mutex);
		pthread_cleanup_push((void (*)(void *)) pthread_mutex_unlock,
				     (void *) md->mdhim_rs->work_queue_mutex);

		//Wait until there is work to be performed
		if ((item = get_work(md)) == NULL) {
			pthread_cond_wait(md->mdhim_rs->work_ready_cv, md->mdhim_rs->work_queue_mutex);
			item = get_work(md);
		}
	       
		pthread_cleanup_pop(0);
		if (!item) {
			pthread_mutex_unlock(md->mdhim_rs->work_queue_mutex);			
			continue;
		}
		pthread_mutex_unlock(md->mdhim_rs->work_queue_mutex);

		//Clean outstanding sends
		range_server_clean_oreqs(md);

		gettimeofday(&worker_start, NULL);
		while (item) {
			//Call the appropriate function depending on the message type			
			//Get the message type
			mtype = ((struct mdhim_basem_t *) item->message)->mtype;

			switch(mtype) {
			case MDHIM_PUT:
				//Pack the put message and pass to range_server_put
				range_server_put(md, 
						 item->message, 
						 item->source);
				break;
			case MDHIM_BULK_PUT:
				//Pack the bulk put message and pass to range_server_put
				gettimeofday(&worker_put_start, NULL);
				range_server_bput(md, 
						  item->message, 
						  item->source);
				gettimeofday(&worker_put_end, NULL);
				worker_put_time += 1000000*(worker_put_end.tv_sec-worker_put_start.tv_sec)+worker_put_end.tv_usec-worker_put_start.tv_usec;
				break;
			case MDHIM_BULK_GET:
				gettimeofday(&worker_get_start, NULL);
				op = ((struct mdhim_bgetm_t *) item->message)->op;
				num_records = ((struct mdhim_bgetm_t *) item->message)->num_recs;
				num_keys = ((struct mdhim_bgetm_t *) item->message)->num_keys;
				//The client is sending one key, but requesting the retrieval of more than one
				if (num_records > 1 && num_keys == 1) {
					range_server_bget_op(md, 
							     item->message, 
							     item->source, op);
				} else {
					range_server_bget(md, 
							  item->message, 
							  item->source);
				}

				gettimeofday(&worker_get_end, NULL);
				worker_get_time += 1000000*(worker_get_end.tv_sec-worker_get_start.tv_sec)+worker_get_end.tv_usec-worker_get_start.tv_usec;
				break;
			case MDHIM_DEL:
				range_server_del(md, item->message, item->source);
				break;
			case MDHIM_BULK_DEL:
				range_server_bdel(md, item->message, item->source);
				break;
			case MDHIM_COMMIT:
				range_server_commit(md, item->message, item->source);
				break;		
			default:
				printf("Rank: %d - Got unknown work type: %d" 
				       " from: %d\n", md->mdhim_rank, mtype, item->source);
				break;
			}
			
			item_tmp = item;
			item = item->next;
			free(item_tmp);
		}

		//Clean outstanding sends
		range_server_clean_oreqs(md);			
		if (putflag == 0) {	
			gettimeofday(&worker_end, NULL);
			gettimeofday(&resp_get_comm_end, NULL);
			resp_get_comm_time+=1000000*(resp_get_comm_end.tv_sec\
			-resp_get_comm_start.tv_sec)+resp_get_comm_end.tv_usec\
			-resp_get_comm_start.tv_usec;
		}
		else {
			gettimeofday(&resp_put_comm_end, NULL);
			resp_put_comm_time+=1000000*(resp_put_comm_end.tv_sec\
			-resp_put_comm_start.tv_sec)+resp_put_comm_end.tv_usec\
			-resp_put_comm_start.tv_usec;	
		}
		worker_time += 1000000*(worker_end.tv_sec-worker_start.tv_sec)+worker_end.tv_usec-worker_start.tv_usec;	
	}
	return NULL;
}

int range_server_add_oreq(struct mdhim_t *md, MPI_Request *req, void *msg) {
	out_req *oreq;
	out_req *item;

	pthread_mutex_lock(md->mdhim_rs->out_req_mutex);
	item = md->mdhim_rs->out_req_list;
	oreq = malloc(sizeof(out_req));
	oreq->next = NULL;
	oreq->prev = NULL;
	oreq->message = msg;
	oreq->req = req;

	if (!item) {
		md->mdhim_rs->out_req_list = oreq;
		pthread_mutex_unlock(md->mdhim_rs->out_req_mutex);
		return MDHIM_SUCCESS;
	}

	item->prev = oreq;
	oreq->next = item;
	md->mdhim_rs->out_req_list = oreq;
	pthread_mutex_unlock(md->mdhim_rs->out_req_mutex);

	return MDHIM_SUCCESS;	
}

int range_server_clean_oreqs(struct mdhim_t *md) {
	out_req *item;
	out_req *t;
	int ret = MDHIM_SUCCESS;
	int flag = 0;
	MPI_Status status;

	pthread_mutex_lock(md->mdhim_rs->out_req_mutex);
	item = md->mdhim_rs->out_req_list;
	while (item) {
		if (!item->req) {
			item = item->next;
			continue;
		}

		pthread_mutex_lock(md->mdhim_comm_lock);
		ret = MPI_Test((MPI_Request *)item->req, &flag, &status); 
		pthread_mutex_unlock(md->mdhim_comm_lock);

		if (ret != MPI_SUCCESS) {
			ret = MDHIM_ERROR;
			break;
		}

		if (!flag) {
			item = item->next;
			continue;
		}
		
		if (item == md->mdhim_rs->out_req_list) {
			md->mdhim_rs->out_req_list = item->next;
			if (item->next) {
				item->next->prev = NULL;
			}
		} else {
			if (item->next) {
				item->next->prev = item->prev;
			}
			if (item->prev) {
				item->prev->next = item->next;
			}
		}

		t = item->next;
		free(item->req);
		if (item->message) {
			free(item->message);
		}

		free(item);
		item = t;
	}

	pthread_mutex_unlock(md->mdhim_rs->out_req_mutex);

	return ret;
}

/**
 * range_server_init
 * Initializes the range server (i.e., starts the threads and populates the relevant data in md)
 *
 * @param md  Pointer to the main MDHIM structure
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int range_server_init(struct mdhim_t *md) {
	int ret;
	int i;

	//Allocate memory for the mdhim_rs_t struct
	md->mdhim_rs = malloc(sizeof(struct mdhim_rs_t));
	if (!md->mdhim_rs) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for range server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	//Initialize variables for printing out timings
	md->mdhim_rs->put_time = 0;
	md->mdhim_rs->get_time = 0;
	md->mdhim_rs->num_put = 0;
	md->mdhim_rs->num_get = 0;
	//Initialize work queue
	md->mdhim_rs->work_queue = malloc(sizeof(work_queue_t));
	md->mdhim_rs->work_queue->head = NULL;
	md->mdhim_rs->work_queue->tail = NULL;

	//Initialize the outstanding request list
	md->mdhim_rs->out_req_list = NULL;

	//Initialize work queue mutex
	md->mdhim_rs->work_queue_mutex = malloc(sizeof(pthread_mutex_t));
	if (!md->mdhim_rs->work_queue_mutex) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for range server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	if ((ret = pthread_mutex_init(md->mdhim_rs->work_queue_mutex, NULL)) != 0) {    
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing work queue mutex", md->mdhim_rank);
		return MDHIM_ERROR;
	}

	//Initialize out req mutex
	md->mdhim_rs->out_req_mutex = malloc(sizeof(pthread_mutex_t));
	if (!md->mdhim_rs->out_req_mutex) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for range server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	if ((ret = pthread_mutex_init(md->mdhim_rs->out_req_mutex, NULL)) != 0) {    
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing out req mutex", md->mdhim_rank);
		return MDHIM_ERROR;
	}

	//Initialize the condition variables
	md->mdhim_rs->work_ready_cv = malloc(sizeof(pthread_cond_t));
	if (!md->mdhim_rs->work_ready_cv) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for range server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	if ((ret = pthread_cond_init(md->mdhim_rs->work_ready_cv, NULL)) != 0) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing condition variable", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	
	//Initialize worker threads
	md->mdhim_rs->workers = malloc(sizeof(pthread_t *) * md->db_opts->num_wthreads);
	for (i = 0; i < md->db_opts->num_wthreads; i++) {
		md->mdhim_rs->workers[i] = malloc(sizeof(pthread_t));
		if ((ret = pthread_create(md->mdhim_rs->workers[i], NULL, 
					  worker_thread, (void *) md)) != 0) {    
			mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
			     "Error while initializing worker thread", 
			     md->mdhim_rank);
			return MDHIM_ERROR;
		}
	}

	//Initialize listener threads
	if ((ret = pthread_create(&md->mdhim_rs->listener, NULL, 
				  listener_thread, (void *) md)) != 0) {
	  mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
	       "Error while initializing listener thread", 
	       md->mdhim_rank);
	  return MDHIM_ERROR;
	}

	return MDHIM_SUCCESS;
}
