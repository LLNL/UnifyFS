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
#include "client.h"
#include "partitioner.h"
#include <sys/time.h>

struct timeval msggetstart, msggetend;
double msggettime=0;

struct timeval msgputstart, msgputend;
double msgputtime=0;
/**
 * Send put to range server
 *
 * @param md main MDHIM struct
 * @param pm pointer to put message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *client_put(struct mdhim_t *md, struct mdhim_putm_t *pm) {

	int return_code;
	struct mdhim_rm_t *rm;       

	return_code = send_rangesrv_work(md, pm->basem.server_rank, pm);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "put record request",  md->mdhim_rank, return_code);
		return NULL;
	}

	return_code = receive_client_response(md, pm->basem.server_rank, (void **) &rm);
	// If the receive did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "put record request",  md->mdhim_rank, return_code);
		rm->error = MDHIM_ERROR;	
	}
        
	// Return response message
	return rm;
}

/**
 * Send bulk put to range server
 * 
 * @param md main MDHIM struct
 * @param bpm_list double pointer to an array of bulk put messages
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_brm_t *client_bput(struct mdhim_t *md, struct index_t *index, 
				struct mdhim_bputm_t **bpm_list) {
	int return_code;
	struct mdhim_brm_t *brm_head, *brm_tail, *brm;
	struct mdhim_rm_t **rm_list, *rm;
	int i;
	int *srvs;
	int num_srvs;

	num_srvs = 0;
	srvs = malloc(sizeof(int) * index->num_rangesrvs);
	for (i = 0; i < index->num_rangesrvs; i++) {
		if (!bpm_list[i]) {
			continue;
		}

		srvs[num_srvs] = bpm_list[i]->basem.server_rank;
		num_srvs++;
	}

	if (!num_srvs) {
	  free(srvs);
	  return NULL;
	}

	gettimeofday(&msgputstart, NULL);
	return_code = send_all_rangesrv_work(md, (void **) bpm_list, index->num_rangesrvs);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "bput record request",  md->mdhim_rank, return_code);

		return NULL;
	}
	gettimeofday(&msgputend, NULL);
	msgputtime += 1000000*(msgputend.tv_sec-msgputstart.tv_sec) + msgputend.tv_usec - msgputstart.tv_usec;
	
	rm_list = malloc(sizeof(struct mdhim_rm_t *) * num_srvs);
	memset(rm_list, 0, sizeof(struct mdhim_rm_t *) * num_srvs);
	return_code = receive_all_client_responses(md, srvs, num_srvs, (void ***) &rm_list);
	// If the receives did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "bput record requests",  md->mdhim_rank, return_code);
	}

	brm_head = brm_tail = NULL;
	for (i = 0; i < num_srvs; i++) {
		rm = rm_list[i];
		if (!rm) {
		  mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		       "Error: did not receive a response message in client_bput",  
		       md->mdhim_rank);
		  //Skip this as the message doesn't exist
		  continue;
		}

		brm = malloc(sizeof(struct mdhim_brm_t));
		brm->error = rm->error;
		brm->basem.mtype = rm->basem.mtype;
		brm->basem.server_rank = rm->basem.server_rank;
		free(rm);

		//Build the linked list to return
		brm->next = NULL;
		if (!brm_head) {
			brm_head = brm;
			brm_tail = brm;
		} else {
			brm_tail->next = brm;
			brm_tail = brm;
		}
	}

	free(rm_list);
	free(srvs);

	// Return response message
	return brm_head;
}

/** Send bulk get to range server
 *
 * @param md main MDHIM struct
 * @param bgm_list double pointer to an array or bulk get messages
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_bgetrm_t *client_bget(struct mdhim_t *md, struct index_t *index, 
				   struct mdhim_bgetm_t **bgm_list) {
	int return_code;
	struct mdhim_bgetrm_t *bgrm_head, *bgrm_tail, *bgrm;
	struct mdhim_bgetrm_t **bgrm_list;
	int i;
	int *srvs;
	int num_srvs;

	num_srvs = 0;
	srvs = malloc(sizeof(int) * index->num_rangesrvs);
	for (i = 0; i < index->num_rangesrvs; i++) {
		if (!bgm_list[i]) {
			continue;
		}

		srvs[num_srvs] = bgm_list[i]->basem.server_rank;
		num_srvs++;
	}

	if (!num_srvs) {
	  free(srvs);
	  return NULL;
	}
	gettimeofday(&msggetstart, NULL);
	return_code = send_all_rangesrv_work(md, (void **) bgm_list, index->num_rangesrvs);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "bget record request",  md->mdhim_rank, return_code);

		return NULL;
	}
	gettimeofday(&msggetend, NULL);
	msggettime += 1000000*(msggetend.tv_sec-msggetstart.tv_sec)+\
		msggetend.tv_usec-msggetstart.tv_usec;
	
	bgrm_list = malloc(sizeof(struct mdhim_bgetrm_t *) * num_srvs);
	memset(bgrm_list, 0, sizeof(struct mdhim_bgetrm_t *) * num_srvs);
	return_code = receive_all_client_responses(md, srvs, num_srvs, (void ***) &bgrm_list);
	// If the receives did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "bget record requests",  md->mdhim_rank, return_code);
	}

	bgrm_head = bgrm_tail = NULL;
	for (i = 0; i < num_srvs; i++) {
		bgrm = bgrm_list[i];
		if (!bgrm) {
		  mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		       "Error: did not receive a response message in client_bget",  
		       md->mdhim_rank);
		  //Skip this as the message doesn't exist
		  continue;
		}
 		//Build the linked list to return
		bgrm->next = NULL;
		if (!bgrm_head) {
			bgrm_head = bgrm;
			bgrm_tail = bgrm;
		} else {
			bgrm_tail->next = bgrm;
			bgrm_tail = bgrm;
		}
	}

	free(bgrm_list);
	free(srvs);

	// Return response message
	return bgrm_head;
}

/** Send get to range server with an op and number of records greater than one
 *
 * @param md main MDHIM struct
 * @param gm pointer to get message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_bgetrm_t *client_bget_op(struct mdhim_t *md, struct mdhim_getm_t *gm) {

	int return_code;
	struct mdhim_bgetrm_t *brm;
	
	return_code = send_rangesrv_work(md, gm->basem.server_rank, gm);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "get record request",  md->mdhim_rank, return_code);
		return NULL;
	}

	return_code = receive_client_response(md, gm->basem.server_rank, (void **) &brm);
	// If the receive did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "get record request",  md->mdhim_rank, return_code);
		brm->error = MDHIM_ERROR;
	}

	// Return response message
	return brm;
}

/**
 * Send delete to range server
 *
 * @param md main MDHIM struct
 * @param dm pointer to del message to be sent or inserted into the range server's work queue
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_rm_t *client_delete(struct mdhim_t *md, struct mdhim_delm_t *dm) {

	int return_code;
	struct mdhim_rm_t *rm;       

	return_code = send_rangesrv_work(md, dm->basem.server_rank, dm);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "delete record request",  md->mdhim_rank, return_code);
		return NULL;
	}

	return_code = receive_client_response(md, dm->basem.server_rank, (void **) &rm);
	// If the receive did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "delete record request",  md->mdhim_rank, return_code);
		rm->error = MDHIM_ERROR;
	}

	// Return response
	return rm;
}

/**
 * Send bulk delete to range server
 *
 * @param md main MDHIM struct
 * @param bdm_list double pointer to an array of bulk del messages
 * @return return_message structure with ->error = MDHIM_SUCCESS or MDHIM_ERROR
 */
struct mdhim_brm_t *client_bdelete(struct mdhim_t *md, struct index_t *index, 
				   struct mdhim_bdelm_t **bdm_list) {
	int return_code;
	struct mdhim_brm_t *brm_head, *brm_tail, *brm;
	struct mdhim_rm_t **rm_list, *rm;
	int i;
	int *srvs;
	int num_srvs;

	num_srvs = 0;
	srvs = malloc(sizeof(int) * index->num_rangesrvs);
	for (i = 0; i < index->num_rangesrvs; i++) {
		if (!bdm_list[i]) {
			continue;
		}

		srvs[num_srvs] = bdm_list[i]->basem.server_rank;
		num_srvs++;
	}

	return_code = send_all_rangesrv_work(md, (void **) bdm_list, index->num_rangesrvs);
	// If the send did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while sending "
		     "bdel record request",  md->mdhim_rank, return_code);

		return NULL;
	}
	
	rm_list = malloc(sizeof(struct mdhim_rm_t *) * num_srvs);
	memset(rm_list, 0, sizeof(struct mdhim_rm_t *) * num_srvs);
	return_code = receive_all_client_responses(md, srvs, num_srvs, (void ***) &rm_list);
	// If the receives did not succeed then log the error code and return MDHIM_ERROR
	if (return_code != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - Error: %d from server while receiving "
		     "bdel record requests",  md->mdhim_rank, return_code);
	}

	brm_head = brm_tail = NULL;
	for (i = 0; i < num_srvs; i++) {
		rm = rm_list[i];
		if (!rm) {
		  mlog(MDHIM_CLIENT_CRIT, "MDHIM Rank: %d - " 
		       "Error: did not receive a response message in client_bdel",  
		       md->mdhim_rank);
		  //Skip this as the message doesn't exist
		  continue;
		}

		brm = malloc(sizeof(struct mdhim_brm_t));
		brm->error = rm->error;
		brm->basem.mtype = rm->basem.mtype;
		brm->basem.server_rank = rm->basem.server_rank;
		free(rm);

		//Build the linked list to return
		brm->next = NULL;
		if (!brm_head) {
			brm_head = brm;
			brm_tail = brm;
		} else {
			brm_tail->next = brm;
			brm_tail = brm;
		}
	}

	free(rm_list);
	free(srvs);

	// Return response message
	return brm_head;
}
