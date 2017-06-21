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

#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <ftw.h>
#include "mdhim.h"
#include "range_server.h"
#include "client.h"
#include "local_client.h"
#include "partitioner.h"
#include "mdhim_options.h"
#include "indexes.h"
#include "mdhim_private.h"


/*! \mainpage MDHIM TNG
 *
 * \section intro_sec Introduction
 *
 * MDHIM TNG is a key/value store for HPC
 *
 */


/**
 * mdhimInit
 * Initializes MDHIM - Collective call
 *
 * @param appComm  the communicator that was passed in from the application (e.g., MPI_COMM_WORLD)
 * @param opts Options structure for DB creation, such as name, and primary key type
 * @return mdhim_t* that contains info about this instance or NULL if there was an error
 */

int dbg_rank;
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
struct mdhim_t *mdhimInit(void *appComm, struct mdhim_options_t *opts) {
	int ret = 0;
	int flag, provided;
	struct mdhim_t *md;
	struct index_t *primary_index;
	MPI_Comm comm;

	if (!opts) {
		//Set default options if no options were passed
	        opts = mdhim_options_init();
                mdhim_options_set_db_path(opts, "/tmp/hng/");
                mdhim_options_set_db_name(opts, "mdhimDb");
                mdhim_options_set_db_type(opts, LEVELDB);
                mdhim_options_set_server_factor(opts, 1);
                mdhim_options_set_max_recs_per_slice(opts, 1000);
                mdhim_options_set_key_type(opts, MDHIM_BYTE_KEY);
                mdhim_options_set_debug_level(opts, MLOG_CRIT);
		mdhim_options_set_num_worker_threads(opts, 30);
	}
	
	//Open mlog - stolen from plfs
	ret = mlog_open((char *)"mdhim", 0,
	        opts->debug_level, opts->debug_level, NULL, 0, MLOG_LOGPID, 0);

	//Check if MPI has been initialized
	if ((ret = MPI_Initialized(&flag)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM - Error while calling MPI_Initialized");
		exit(1);
	}      
	if (!flag) {
		//Initialize MPI with multiple thread support since MPI hasn't been initialized
		ret = MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
		if (ret != MPI_SUCCESS) {
			mlog(MDHIM_CLIENT_CRIT, "MDHIM - Error while calling MPI_Init_thread");
			exit(1);
		}
		//Quit if MPI didn't initialize with multiple threads
		if (provided != MPI_THREAD_MULTIPLE) {
			mlog(MDHIM_CLIENT_CRIT, "MDHIM - Error while initializing MPI with threads");
			exit(1);
		}
	}

	if (appComm) {
		comm = *((MPI_Comm *) appComm);
	} else {
		comm = MPI_COMM_WORLD;
	}
	
	//Allocate memory for the main MDHIM structure
	md = malloc(sizeof(struct mdhim_t));
	memset(md, 0, sizeof(struct mdhim_t));
	if (!md) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM - Error while allocating memory while initializing");
		return NULL;
	}

	//Set the options passed or the defaults created
	md->db_opts = opts;

	if ((ret = MPI_Comm_dup(comm, &md->mdhim_comm)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error while initializing the MDHIM communicator");
		return NULL;
	}

	//Get our rank in the main MDHIM communicator
	if ((ret = MPI_Comm_rank(md->mdhim_comm, &md->mdhim_rank)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error getting our rank while initializing MDHIM");
		return NULL;
	}

	//Initialize mdhim_comm mutex
	md->mdhim_comm_lock = malloc(sizeof(pthread_mutex_t));
	if (!md->mdhim_comm_lock) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for client", 
		     md->mdhim_rank);
		return NULL;
	}

	if ((ret = pthread_mutex_init(md->mdhim_comm_lock, NULL)) != 0) {    
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing mdhim_comm_lock", md->mdhim_rank);
		return NULL;
	}

	//Dup the communicator passed in for barriers between clients
	if ((ret = MPI_Comm_dup(comm, &md->mdhim_client_comm)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error while initializing the MDHIM communicator");
		return NULL;
	}

	//Get the size of the main MDHIM communicator
	if ((ret = MPI_Comm_size(md->mdhim_comm, &md->mdhim_comm_size)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error getting the size of the " 
		     "comm while initializing", 
		     md->mdhim_rank);
		return NULL;
	}

	if ((ret = MPI_Comm_rank(md->mdhim_comm, &dbg_rank)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error getting the rank of the "
		     "comm while initializing",
		     md->mdhim_rank);
		return NULL;
	}

	//Initialize receive msg mutex - used for receiving a message from myself
	md->receive_msg_mutex = malloc(sizeof(pthread_mutex_t));
	if (!md->receive_msg_mutex) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for client", 
		     md->mdhim_rank);
		return NULL;
	}
	if ((ret = pthread_mutex_init(md->receive_msg_mutex, NULL)) != 0) {    
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing receive queue mutex", md->mdhim_rank);
		return NULL;
	}
	//Initialize the receive condition variable - used for receiving a message from myself
	md->receive_msg_ready_cv = malloc(sizeof(pthread_cond_t));
	if (!md->receive_msg_ready_cv) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while allocating memory for client", 
		     md->mdhim_rank);
		return NULL;
	}
	if ((ret = pthread_cond_init(md->receive_msg_ready_cv, NULL)) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing client receive condition variable", 
		     md->mdhim_rank);
		return NULL;
	}

	//Initialize the partitioner
	partitioner_init();

	//Initialize the indexes and create the primary index
	md->indexes = NULL;
	md->indexes_by_name = NULL;
	md->indexes_lock = malloc(sizeof(pthread_rwlock_t));
	if (pthread_rwlock_init(md->indexes_lock, NULL) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing remote_indexes_lock", 
		     md->mdhim_rank);
		return NULL;
	}

	//Create the default remote primary index
	primary_index = create_global_index(md, opts->rserver_factor, opts->max_recs_per_slice, 
					    opts->db_type, opts->db_key_type, NULL);
	if (!primary_index) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Couldn't create the default index", 
		     md->mdhim_rank);
		return NULL;
	}
	md->primary_index = primary_index;
	
	//Set the local receive queue to NULL - used for sending and receiving to/from ourselves
	md->receive_msg = NULL;
	MPI_Barrier(md->mdhim_client_comm);


	return md;
}

/**
 * Quits the MDHIM instance - collective call
 *
 * @param md main MDHIM struct
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int mdhimClose(struct mdhim_t *md) {
	int ret;
	struct timeval start, end;

	mlog(MDHIM_CLIENT_DBG, "MDHIM Rank %d: Called close", md->mdhim_rank);
	gettimeofday(&start, NULL);
	MPI_Barrier(md->mdhim_client_comm);
	gettimeofday(&end, NULL);
//	printf("Took: %lu seconds to complete first close barrier\n", end.tv_sec - start.tv_sec);

	gettimeofday(&start, NULL);
	//Stop range server if I'm a range server	
	if (md->mdhim_rs && (ret = range_server_stop(md)) != MDHIM_SUCCESS) {
		return MDHIM_ERROR;
	}
	
	gettimeofday(&end, NULL);
//	printf("Took: %lu seconds to stop the range server\n", end.tv_sec - start.tv_sec);

	//Free up memory used by the partitioner
	partitioner_release();

	//Free up memory used by indexes
	indexes_release(md);

	//Destroy the receive condition variable
	if ((ret = pthread_cond_destroy(md->receive_msg_ready_cv)) != 0) {
		return MDHIM_ERROR;
	}
	free(md->receive_msg_ready_cv);

	//Destroy the receive mutex
	if ((ret = pthread_mutex_destroy(md->receive_msg_mutex)) != 0) {
		return MDHIM_ERROR;
	}
	free(md->receive_msg_mutex);    

	if ((ret = pthread_rwlock_destroy(md->indexes_lock)) != 0) {
		return MDHIM_ERROR;
	}
	free(md->indexes_lock);

	gettimeofday(&start, NULL);
	MPI_Barrier(md->mdhim_client_comm);
	//Destroy the client_comm_lock
	if ((ret = pthread_mutex_destroy(md->mdhim_comm_lock)) != 0) {
		return MDHIM_ERROR;
	}
	gettimeofday(&end, NULL);
	free(md->mdhim_comm_lock);    
//	printf("Took: %lu seconds to complete the second close barrier\n", end.tv_sec - start.tv_sec);
	mlog(MDHIM_CLIENT_DBG, "MDHIM Rank %d: Finished close", md->mdhim_rank);

	MPI_Comm_free(&md->mdhim_client_comm);
	MPI_Comm_free(&md->mdhim_comm);
        free(md);

	//Close MLog
	mlog_close();

	return MDHIM_SUCCESS;
}

/**
 * Commits outstanding MDHIM writes - collective call
 *
 * @param md main MDHIM struct
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int mdhimCommit(struct mdhim_t *md, struct index_t *index) {
	int ret = MDHIM_SUCCESS;
	struct mdhim_basem_t *cm;
	struct mdhim_rm_t *rm = NULL;

	MPI_Barrier(md->mdhim_client_comm);      
	//If I'm a range server, send a commit message to myself
	if (im_range_server(index)) {       
		cm = malloc(sizeof(struct mdhim_basem_t));
		cm->mtype = MDHIM_COMMIT;
		cm->index = index->id;
		cm->index_type = index->type;
		rm = local_client_commit(md, cm);
		if (!rm || rm->error) {
			ret = MDHIM_ERROR;
			mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
			     "Error while committing database in mdhimCommit",
			     md->mdhim_rank);
		}

		if (rm) {
			free(rm);
		}
	}

	MPI_Barrier(md->mdhim_client_comm);      

	return ret;
}

/**
 * Inserts a single record into MDHIM
 *
 * @param md main MDHIM struct
 * @param primary_key        pointer to key to store
 * @param primary_key_len    the length of the key
 * @param value              pointer to the value to store
 * @param value_len          the length of the value
 * @param secondary_info     secondary global and local information for 
                             inserting secondary global and local keys
 * @return                   mdhim_brm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimPut(struct mdhim_t *md,
			     /*Primary key */
			     void *primary_key, int primary_key_len,  
			     void *value, int value_len,
			     /* Optional secondary global and local keys */
			     struct secondary_info *secondary_global_info,
			     struct secondary_info *secondary_local_info) {
	int i;
	//Return message list
	struct mdhim_brm_t *head;
	void **primary_keys;
	int *primary_key_lens;
	//Return message from each _put_record casll
	struct mdhim_brm_t *brm;
	struct mdhim_rm_t *rm;

	brm = NULL;
	rm = NULL;
	head = NULL;
	if (!primary_key || !primary_key_len ||
	    !value || !value_len) {
		return NULL;
	}

	rm = _put_record(md, md->primary_index, primary_key, primary_key_len, value, value_len);
	if (!rm || rm->error) {
		return head;
	}

	head = _create_brm(rm);
	mdhim_full_release_msg(rm);

	//Insert the secondary local key if it was given
	if (secondary_local_info && secondary_local_info->secondary_index && 
	    secondary_local_info->secondary_keys && 
	    secondary_local_info->secondary_key_lens && 
	    secondary_local_info->num_keys) {
		primary_keys = malloc(sizeof(void *) * secondary_local_info->num_keys);
		primary_key_lens = malloc(sizeof(int) * secondary_local_info->num_keys);
		for (i = 0; i < secondary_local_info->num_keys; i++) {
			primary_keys[i] = primary_key;
			primary_key_lens[i] = primary_key_len;
		}

		brm = _bput_records(md, secondary_local_info->secondary_index, 
				    secondary_local_info->secondary_keys, 
				    secondary_local_info->secondary_key_lens, 
				    primary_keys, primary_key_lens, 
				    secondary_local_info->num_keys);

		free(primary_keys);
		free(primary_key_lens);
		if (!brm) {
			return head;
		}
		
		_concat_brm(head, brm);
	}

	//Insert the secondary global key if it was given
	if (secondary_global_info && secondary_global_info->secondary_index && 
	    secondary_global_info->secondary_keys && 
	    secondary_global_info->secondary_key_lens &&
	    secondary_global_info->num_keys) {
		primary_keys = malloc(sizeof(void *) * secondary_global_info->num_keys);
		primary_key_lens = malloc(sizeof(int) * secondary_global_info->num_keys);
		for (i = 0; i < secondary_global_info->num_keys; i++) {
			primary_keys[i] = primary_key;
			primary_key_lens[i] = primary_key_len;
		}
		brm = _bput_records(md, secondary_global_info->secondary_index, 
				    secondary_global_info->secondary_keys, 
				    secondary_global_info->secondary_key_lens, 
				    primary_keys, primary_key_lens,
				    secondary_global_info->num_keys);

		free(primary_keys);
		free(primary_key_lens);
		if (!brm) {
		  return head;
		}

		_concat_brm(head, brm);
	}	

	return head;
}

/**
 * Inserts a single record into an MDHIM secondary index
 *
 * @param md main MDHIM struct
 * @param secondary_key       pointer to key to store
 * @param secondary_key_len   the length of the key
 * @param primary_key     pointer to the primary_key 
 * @param primary_key_len the length of the value
 * @return mdhim_brm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimPutSecondary(struct mdhim_t *md, 
				      struct index_t *secondary_index,
				      /*Secondary key */
				      void *secondary_key, int secondary_key_len,  
				      /* Primary key */
				      void *primary_key, int primary_key_len) {

	//Return message list
	struct mdhim_brm_t *head;

	//Return message from each _put_record casll
	struct mdhim_rm_t *rm;

	rm = NULL;
	head = NULL;
	if (!secondary_key || !secondary_key_len ||
	    !primary_key || !primary_key_len) {
		return NULL;
	}

	rm = _put_record(md, secondary_index, secondary_key, secondary_key_len, 
			 primary_key, primary_key_len);
	if (!rm || rm->error) {
		return head;
	}

	head = _create_brm(rm);
	mdhim_full_release_msg(rm);
	
	return head;
}

struct mdhim_brm_t *_bput_secondary_keys_from_info(struct mdhim_t *md, 
						   struct secondary_bulk_info *secondary_info, 
						   void **primary_keys, int *primary_key_lens, 
						   int num_records) {
	int i, j;
	void **primary_keys_to_send;
	int *primary_key_lens_to_send;
	struct mdhim_brm_t *head, *new;

	head = new = NULL;
	for (i = 0; i < num_records; i++) {
		primary_keys_to_send = 
			malloc(secondary_info->num_keys[i] * sizeof(void *));
		primary_key_lens_to_send = 
			malloc(secondary_info->num_keys[i] * sizeof(int));
			
		for (j = 0; j < secondary_info->num_keys[i]; j++) {
			primary_keys_to_send[j] = primary_keys[i];
			primary_key_lens_to_send[j] = primary_key_lens[i];
		}
		
		new = _bput_records(md, secondary_info->secondary_index, 
				    secondary_info->secondary_keys[i], 
				    secondary_info->secondary_key_lens[i], 
				    primary_keys_to_send, primary_key_lens_to_send, 
				    secondary_info->num_keys[i]);
		if (!head) {
			head = new;
		} else if (new) {
			_concat_brm(head, new);
		}

		free(primary_keys_to_send);
		free(primary_key_lens_to_send);
	}

	return head;
}

/**
 * Inserts multiple records into MDHIM
 * 
 * @param md main MDHIM struct
 * @param keys         pointer to array of keys to store
 * @param key_lens     array with lengths of each key in keys
 * @param values       pointer to array of values to store
 * @param value_lens   array with lengths of each value
 * @param num_records  the number of records to store (i.e., the number of keys in keys array)
 * @return mdhim_brm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimBPut(struct mdhim_t *md, 
			      void **primary_keys, int *primary_key_lens, 
			      void **primary_values, int *primary_value_lens, 
			      int num_records,
			      struct secondary_bulk_info *secondary_global_info,
			      struct secondary_bulk_info *secondary_local_info) {
	struct mdhim_brm_t *head, *new;

	head = new = NULL;
	if (!primary_keys || !primary_key_lens ||
	    !primary_values || !primary_value_lens) {
		return NULL;
	}

	head = _bput_records(md, md->primary_index, primary_keys, primary_key_lens, 
			     primary_values, primary_value_lens, num_records);
	if (!head || head->error) {
		return head;
	}

	//Insert the secondary local keys if they were given
	if (secondary_local_info && secondary_local_info->secondary_index && 
	    secondary_local_info->secondary_keys && 
	    secondary_local_info->secondary_key_lens) {
		new = _bput_secondary_keys_from_info(md, secondary_local_info, primary_keys, 
						     primary_key_lens, num_records);
		if (new) {
			_concat_brm(head, new);
		}	       
	}
	
	//Insert the secondary global keys if they were given
	if (secondary_global_info && secondary_global_info->secondary_index && 
	    secondary_global_info->secondary_keys && 
	    secondary_global_info->secondary_key_lens) {
		new = _bput_secondary_keys_from_info(md, secondary_global_info, primary_keys, 
						     primary_key_lens, num_records);
		if (new) {
			_concat_brm(head, new);
		}	     
	}	

	return head;
}

/**
 * Inserts multiple records into an MDHIM secondary index
 * 
 * @param md           main MDHIM struct
 * @param index        the secondary index to use
 * @param keys         pointer to array of keys to store
 * @param key_lens     array with lengths of each key in keys
 * @param values       pointer to array of values to store
 * @param value_lens   array with lengths of each value
 * @param num_records  the number of records to store (i.e., the number of keys in keys array)
 * @return mdhim_brm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimBPutSecondary(struct mdhim_t *md, struct index_t *secondary_index,
				       void **secondary_keys, int *secondary_key_lens, 
				       void **primary_keys, int *primary_key_lens, 
				       int num_records) {
	struct mdhim_brm_t *head, *new;

	head = new = NULL;
	if (!secondary_keys || !secondary_key_lens || 
	    !primary_keys || !primary_key_lens) {
		return NULL;
	}

	head = _bput_records(md, secondary_index, secondary_keys, secondary_key_lens, 
			     primary_keys, primary_key_lens, num_records);
	if (!head || head->error) {
		return head;
	}

	return head;
}

/**
 * Retrieves a single record from MDHIM
 *
 * @param md main MDHIM struct
 * @param key       pointer to key to get value of or last key to start from if op is 
 (MDHIM_GET_NEXT or MDHIM_GET_PREV)
 * @param key_len   the length of the key
 * @param op        the operation type
 * @return mdhim_getrm_t * or NULL on error
 */
struct mdhim_bgetrm_t *mdhimGet(struct mdhim_t *md, struct index_t *index,
				void *key, int key_len, 
				int op) {

	void **keys;
	int *key_lens;
	struct mdhim_bgetrm_t *bgrm_head;

	if (op != MDHIM_GET_EQ && op != MDHIM_GET_PRIMARY_EQ) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Invalid op specified for mdhimGet", 
		     md->mdhim_rank);
		return NULL;
	}

	if (!index) {
		index = md->primary_index;
	}

	//Create an a array with the single key and key len passed in
	keys = malloc(sizeof(void *));
	key_lens = malloc(sizeof(int));
	keys[0] = key;
	key_lens[0] = key_len;

	//Get the linked list of return messages from mdhimBGet
	bgrm_head = _bget_records(md, index, keys, key_lens, 1, 1, op);

	//Clean up
	free(keys);
	free(key_lens);

	return bgrm_head;
}

/**
 * Retrieves multiple records from MDHIM
 *
 * @param md main MDHIM struct
 * @param keys         pointer to array of keys to get values for
 * @param key_lens     array with lengths of each key in keys
 * @param num_records  the number of keys to get (i.e., the number of keys in keys array)
 * @return mdhim_bgetrm_t * or NULL on error
 */
struct mdhim_bgetrm_t *mdhimBGet(struct mdhim_t *md, struct index_t *index,
				 void **keys, int *key_lens, 
				 int num_keys, int op) {
	struct mdhim_bgetrm_t *bgrm_head, *lbgrm;
	void **primary_keys;
	int *primary_key_lens, plen;
	struct index_t *primary_index;
	int i;

	if (op != MDHIM_GET_EQ && op != MDHIM_GET_PRIMARY_EQ && op != MDHIM_RANGE_BGET) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Invalid operation for mdhimBGet", 
		     md->mdhim_rank);
		return NULL;
	}

	//Check to see that we were given a sane amount of records
	if (num_keys > MAX_BULK_OPS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Too many bulk operations requested in mdhimBGet", 
		     md->mdhim_rank);
		return NULL;
	}

	if (!index) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Invalid index specified", 
		     md->mdhim_rank);
		return NULL;
	}

	bgrm_head = _bget_records(md, index, keys, key_lens, num_keys, 1, op);
	if (!bgrm_head) {
		return NULL;
	}

	if (op == MDHIM_GET_PRIMARY_EQ) {
		//Get the number of keys/values we received
		plen = 0;
		while (bgrm_head) {
			for (i = 0; i < bgrm_head->num_keys; i++) {	      
				plen++;					
			}
			
			bgrm_head = bgrm_head->next;
		}

		if (plen > MAX_BULK_OPS) {
			printf("plen is %ld, MAX_BULK_OPS is %ld\n", plen, MAX_BULK_OPS);
			fflush(stdout);
			mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
			     "Too many bulk operations would be performed " 
			     "with the MDHIM_GET_PRIMARY_EQ operation.  Limiting "
			     "request to : %u key/values", 
			     md->mdhim_rank, MAX_BULK_OPS);
			plen = MAX_BULK_OPS - 1;
		}

		primary_keys = malloc(sizeof(void *) * plen);
		primary_key_lens = malloc(sizeof(int) * plen);
		//Initialize the primary keys array and key lens array
		memset(primary_keys, 0, sizeof(void *) * plen);
		memset(primary_key_lens, 0, sizeof(int) * plen);
		
		//Get the primary keys from the previously received messages' values
		plen = 0;
		while (bgrm_head) {
			for (i = 0; i < bgrm_head->num_keys && plen < MAX_BULK_OPS ; i++) {
				primary_keys[plen] = malloc(bgrm_head->value_lens[i]);
				memcpy(primary_keys[plen], bgrm_head->values[i], 
				       bgrm_head->value_lens[i]);
				primary_key_lens[plen] = bgrm_head->value_lens[i];			
				plen++;					
			}

			lbgrm = bgrm_head->next;
			mdhim_full_release_msg(bgrm_head);
			bgrm_head = lbgrm;
		}

		primary_index = get_index(md, index->primary_id);	
		//Get the primary keys' values
		bgrm_head = _bget_records(md, primary_index,
					  primary_keys, primary_key_lens, 
					  plen, 1, MDHIM_GET_EQ);

		//Free up the primary keys and lens arrays
		for (i = 0; i < plen; i++) {
			free(primary_keys[i]);			
		}

		free(primary_keys);
		free(primary_key_lens);
	}

	//Return the head of the list
	return bgrm_head;
}


/**
 * Retrieves multiple sequential records from a single range server if they exist
 * 
 * If the operation passed in is MDHIM_GET_NEXT or MDHIM_GET_PREV, this return all the records
 * starting from the key passed in in the direction specified
 *
 * If the operation passed in is MDHIM_GET_FIRST and MDHIM_GET_LAST and the key is NULL,
 * then this operation will return the keys starting from the first or last key
 *
 * If the operation passed in is MDHIM_GET_FIRST and MDHIM_GET_LAST and the key is not NULL,
 * then this operation will return the keys starting the first key on 
 * the range server that the key resolves to
 *
 * @param md           main MDHIM struct
 * @param key          pointer to the key to start getting next entries from
 * @param key_len      the length of the key
 * @param num_records  the number of successive keys to get
 * @param op           the operation to perform (i.e., MDHIM_GET_NEXT or MDHIM_GET_PREV)
 * @return mdhim_bgetrm_t * or NULL on error
 */
struct mdhim_bgetrm_t *mdhimBGetOp(struct mdhim_t *md, struct index_t *index, 
				   void *key, int key_len, 
				   int num_records, int op) {
	void **keys;
	int *key_lens;
	struct mdhim_bgetrm_t *bgrm_head;
	printf("num_records is is %ld, MAX_BULK_OPS is %ld\n", num_records, MAX_BULK_OPS);
	fflush(stdout);

	if (num_records > MAX_BULK_OPS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "To many bulk operations requested in mdhimBGetOp", 
		     md->mdhim_rank);
		return NULL;
	}

	if (op == MDHIM_GET_EQ || op == MDHIM_GET_PRIMARY_EQ) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Invalid op specified for mdhimGet", 
		     md->mdhim_rank);
		return NULL;
	}

	//Create an a array with the single key and key len passed in
	keys = malloc(sizeof(void *));
	key_lens = malloc(sizeof(int));
	keys[0] = key;
	key_lens[0] = key_len;

	//Get the linked list of return messages from mdhimBGet
	bgrm_head = _bget_records(md, index, keys, key_lens, 1, num_records, op);

	//Clean up
	free(keys);
	free(key_lens);

	return bgrm_head;
}

struct mdhim_bgetrm_t *mdhimBGetRange(struct mdhim_t *md, struct index_t *index,
				   void *start_key, void *end_key, int key_len) {
	struct mdhim_bgetrm_t *bgrm_head;

	//Get the linked list of return messages from mdhimBGet
	bgrm_head = _bget_range_records(md, index, start_key, end_key, key_len);

	return bgrm_head;
}



/**
 * Deletes a single record from MDHIM
 *
 * @param md main MDHIM struct
 * @param key       pointer to key to delete
 * @param key_len   the length of the key
 * @return mdhim_rm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimDelete(struct mdhim_t *md, struct index_t *index, 
				void *key, int key_len) {
	struct mdhim_brm_t *brm_head;
	void **keys;
	int *key_lens;
	
	keys = malloc(sizeof(void *));
	key_lens = malloc(sizeof(int));
	keys[0] = key;
	key_lens[0] = key_len;

	brm_head = _bdel_records(md, index, keys, key_lens, 1);
	
	free(keys);
	free(key_lens);

	return brm_head;
}

/**
 * Deletes multiple records from MDHIM
 *
 * @param md main MDHIM struct
 * @param keys         pointer to array of keys to delete
 * @param key_lens     array with lengths of each key in keys
 * @param num_records  the number of keys to delete (i.e., the number of keys in keys array)
 * @return mdhim_brm_t * or NULL on error
 */
struct mdhim_brm_t *mdhimBDelete(struct mdhim_t *md, struct index_t *index,
				 void **keys, int *key_lens,
				 int num_records) {
	struct mdhim_brm_t *brm_head;


	//Check to see that we were given a sane amount of records
	if (num_records > MAX_BULK_OPS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "To many bulk operations requested in mdhimBGetOp", 
		     md->mdhim_rank);
		return NULL;
	}

	brm_head = _bdel_records(md, index, keys, key_lens, num_records);

	//Return the head of the list
	return brm_head;
}

int unlink_cb(const char *fpath,\
		const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);

    if (rv)
        perror(fpath);

    return rv;
}

int rmrf(char *path)
{
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

int mdhimSanitize(char *dbfilename, char *statfilename, char *manifestfilename) {
	int rc = 0;

	rc = rmrf(dbfilename);
	rc = rmrf(statfilename);
	rc = unlink(manifestfilename);

	return rc;

}


/**
 * Retrieves statistics from all the range servers - collective call
 *
 * @param md main MDHIM struct
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int mdhimStatFlush(struct mdhim_t *md, struct index_t *index) {
	int ret;

	MPI_Barrier(md->mdhim_client_comm);	
	if ((ret = get_stat_flush(md, index)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		     "Error while getting MDHIM stat data in mdhimStatFlush", 
		     md->mdhim_rank);
	}
	MPI_Barrier(md->mdhim_client_comm);	

	return ret;
}

/**
 * Sets the secondary_info structure used in mdhimPut
 *
 */
struct secondary_info *mdhimCreateSecondaryInfo(struct index_t *secondary_index,
						void **secondary_keys, int *secondary_key_lens,
						int num_keys, int info_type) {
	struct secondary_info *sinfo;
	

	if (!secondary_index || !secondary_keys || 
	    !secondary_key_lens || !num_keys) {
		return NULL;
	}

	if (info_type != SECONDARY_GLOBAL_INFO && 
	    info_type != SECONDARY_LOCAL_INFO) {
		return NULL;
	}

	//Initialize the struct
	sinfo = malloc(sizeof(struct secondary_info));
	memset(sinfo, 0, sizeof(struct secondary_info));
	
	//Set the index fields 
	sinfo->secondary_index = secondary_index;
	sinfo->secondary_keys = secondary_keys;
	sinfo->secondary_key_lens = secondary_key_lens;
	sinfo->num_keys = num_keys;
	sinfo->info_type = info_type;

	return sinfo;
}

void mdhimReleaseSecondaryInfo(struct secondary_info *si) {
	free(si);

	return;
}

/**
 * Sets the secondary_info structure used in mdhimBPut
 *
 */
struct secondary_bulk_info *mdhimCreateSecondaryBulkInfo(struct index_t *secondary_index,
							 void ***secondary_keys, 
							 int **secondary_key_lens,
							 int *num_keys, int info_type) {

	struct secondary_bulk_info *sinfo;
	
	if (!secondary_index || !secondary_keys || 
	    !secondary_key_lens || !num_keys) {
		return NULL;
	}

	if (info_type != SECONDARY_GLOBAL_INFO && 
	    info_type != SECONDARY_LOCAL_INFO) {
		return NULL;
	}

	//Initialize the struct
	sinfo = malloc(sizeof(struct secondary_bulk_info));
	memset(sinfo, 0, sizeof(struct secondary_bulk_info));
	
	//Set the index fields 
	sinfo->secondary_index = secondary_index;
	sinfo->secondary_keys = secondary_keys;
	sinfo->secondary_key_lens = secondary_key_lens;
	sinfo->num_keys = num_keys;
	sinfo->info_type = info_type;

	return sinfo;
}

void mdhimReleaseSecondaryBulkInfo(struct secondary_bulk_info *si) {
	free(si);

	return;
}
