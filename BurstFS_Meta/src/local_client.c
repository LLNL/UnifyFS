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

#include <stdlib.h>
#include "mdhim.h"
#include "local_client.h"

/**
 * get_msg_self
 * Gets a message from the range server if we are waiting to hear back from ourselves 
 * This means that the range server is running in the same process as the caller, 
 * but on a different thread  
 *
 * @param md the main mdhim struct
 * @return a pointer to the message received or NULL
 */
static void *get_msg_self(struct mdhim_t *md) {
	void *msg;
	
	//Lock the receive msg mutex
	pthread_mutex_lock(md->receive_msg_mutex);
	//Wait until there is a message to receive
	if (!md->receive_msg) {
		pthread_cond_wait(md->receive_msg_ready_cv, md->receive_msg_mutex);
	}
	
	//Get the message
	msg = md->receive_msg;
	//Set the message queue to null
	md->receive_msg = NULL;
	//unlock the mutex
	pthread_mutex_unlock(md->receive_msg_mutex);
	
	return msg;
}

/**
 * Send put to range server
 *
 * @param md main MDHIM struct
 * @param pm pointer to put message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *local_client_put(struct mdhim_t *md, struct mdhim_putm_t *pm) {
	int ret;
	struct mdhim_rm_t *rm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	memset(item, 0, sizeof(work_item));
	item->message = (void *)pm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	rm = (struct mdhim_rm_t *) get_msg_self(md);
	// Return response

	return rm;
}

/**
 * Send bulk put to range server
 * 
 * @param md main MDHIM struct
 * @param bpm pointer to bulk put message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
*/
struct mdhim_rm_t *local_client_bput(struct mdhim_t *md, struct mdhim_bputm_t *bpm) {
	int ret;
	struct mdhim_rm_t *brm;
	work_item *item;
        
	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)bpm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	brm = (struct mdhim_rm_t *) get_msg_self(md);

	// Return response
	return brm;
}

/**
 * Send bulk get to range server
 *
 * @param md main MDHIM struct
 * @param bgm pointer to get message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_bgetrm_t *local_client_bget(struct mdhim_t *md, struct mdhim_bgetm_t *bgm) {
	int ret;
	struct mdhim_bgetrm_t *rm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)bgm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	rm = (struct mdhim_bgetrm_t *) get_msg_self(md);

	// Return response
	return rm;
}

/**
 * Send get with an op and number of records greater than 1 to range server
 *
 * @param md main MDHIM struct
 * @param gm pointer to get message to be inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_bgetrm_t *local_client_bget_op(struct mdhim_t *md, struct mdhim_getm_t *gm) {
	int ret;
	struct mdhim_bgetrm_t *rm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)gm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	rm = (struct mdhim_bgetrm_t *) get_msg_self(md);

	// Return response
	return rm;
}

/**
 * Send commit to range server
 *
 * @param md main MDHIM struct
 * @param cm pointer to put message to be inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *local_client_commit(struct mdhim_t *md, struct mdhim_basem_t *cm) {
	int ret;
	struct mdhim_rm_t *rm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)cm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	rm = (struct mdhim_rm_t *) get_msg_self(md);
	// Return response

	return rm;
}

/**
 * Send delete to range server
 *
 * @param md main MDHIM struct
 * @param dm pointer to delete message to be inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *local_client_delete(struct mdhim_t *md, struct mdhim_delm_t *dm) {
	int ret;
	struct mdhim_rm_t *rm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)dm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	rm = (struct mdhim_rm_t *) get_msg_self(md);

	// Return response
	return rm;

}

/**
 * Send bulk delete to MDHIM
 *
 * @param md main MDHIM struct
 * @param bdm pointer to bulk delete message to be inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *local_client_bdelete(struct mdhim_t *md, struct mdhim_bdelm_t *bdm) {
	int ret;
	struct mdhim_rm_t *brm;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return NULL;
	}

	item->message = (void *)bdm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return NULL;
	}
	
	brm = (struct mdhim_rm_t *) get_msg_self(md);

	// Return response
	return brm;
}

/**
 * Send close to range server
 *
 * @param md main MDHIM struct
 * @param cm pointer to close message to be inserted into the range server's work queue
 */
void local_client_close(struct mdhim_t *md, struct mdhim_basem_t *cm) {
	int ret;
	work_item *item;

	if ((item = malloc(sizeof(work_item))) == NULL) {
		mlog(MDHIM_CLIENT_CRIT, "Error while allocating memory for client");
		return;
	}

	item->message = (void *)cm;
	item->source = md->mdhim_rank;
	if ((ret = range_server_add_work(md, item)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Error adding work to range server in local_client_put");
		return;
	}
	
	return;
}
