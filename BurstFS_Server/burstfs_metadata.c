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

#include "mdhim.h"
#include "indexes.h"
#include "log.h"
#include "burstfs_metadata.h"
#include "arraylist.h"
#include "burstfs_const.h"
#include "burstfs_global.h"

burstfs_key_t **burstfs_keys;
burstfs_val_t **burstfs_vals;

fattr_key_t **fattr_keys;
fattr_val_t **fattr_vals;

char *manifest_path;

struct mdhim_brm_t *brm, *brmp;
struct mdhim_bgetrm_t *bgrm, *bgrmp;

mdhim_options_t *db_opts;
struct mdhim_t *md;

int md_size;
int burstfs_key_lens[MAX_META_PER_SEND] = {0};
int burstfs_val_lens[MAX_META_PER_SEND] = {0};

int fattr_key_lens[MAX_FILE_CNT_PER_NODE] = {0};
int fattr_val_lens[MAX_FILE_CNT_PER_NODE] = {0};

struct index_t *burstfs_indexes[2];
long max_recs_per_slice;
/**
* initialize the key-value store
*/
int meta_init_store() {
	db_opts = malloc(sizeof(struct mdhim_options_t));
	if (!db_opts)
		return -1;

	/* BURSTFS_META_DB_PATH: file that stores the key value pair*/
	char *env = getenv("BURSTFS_META_DB_PATH");
	if (!env) {
		db_opts->db_path = malloc(strlen(DEF_META_PATH) + 1);
		if (!db_opts->db_path)
			return -1;

		strcpy(db_opts->db_path, DEF_META_PATH);
	} else {
		db_opts->db_path = malloc(strlen(env) + 1);
		if (!db_opts->db_path)
			return -1;

		strcpy(db_opts->db_path, env);
	}

	db_opts->manifest_path = NULL;
	db_opts->db_type = LEVELDB;
	db_opts->db_create_new = 1;

	/* META_SERVER_RATIO: number of metadata servers =
		number of processes/META_SERVER_RATIO */

	int ser_ratio;
	env = getenv("BURSTFS_META_SERVER_RATIO");
	if (!env)
		ser_ratio = DEF_SERVER_RATIO;

	ser_ratio = atoi(env);


	db_opts->rserver_factor = ser_ratio;
	db_opts->db_paths = NULL;
	db_opts->num_paths = 0;
	db_opts->num_wthreads = 1;

	int path_len = strlen(db_opts->db_path) + strlen(MANIFEST_FILE_NAME) + 1;


	manifest_path = malloc(path_len);
	if (!manifest_path)
		return -1;

	sprintf(manifest_path, "%s%s", db_opts->db_path, MANIFEST_FILE_NAME);
	db_opts->manifest_path = manifest_path;

	env = getenv("BURSTFS_META_DB_NAME");
	if (!env) {
		db_opts->db_name = malloc(strlen(DEF_DB_NAME) + 1);
		if (!db_opts->db_name)
			return -1;

		strcpy(db_opts->db_name, DEF_DB_NAME);
	} else {
		db_opts->db_name = malloc(strlen(env) + 1);
		if (!db_opts->db_name)
			return -1;

		strcpy(db_opts->db_name, env);
	}

	db_opts->db_key_type = MDHIM_BURSTFS_KEY;
	db_opts->debug_level = MLOG_CRIT;

	/* indices/attributes are striped to servers according
	 * to BurstFS_META_RANGE_SZ.
	 * */
	env = getenv("BURSTFS_META_RANGE_SZ");
	if (!env)
		db_opts->max_recs_per_slice = DEF_RANGE_SZ;
	else
		db_opts->max_recs_per_slice = atol(env);

	max_recs_per_slice = db_opts->max_recs_per_slice;

	MPI_Comm comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);

	/*this index is created for storing index metadata*/
	burstfs_indexes[0] = md->primary_index;

	/*this index is created for storing file attribute metadata*/
	burstfs_indexes[1] = create_global_index(md, ser_ratio, 1,\
			LEVELDB, MDHIM_INT_KEY, "file_attr");

	MPI_Comm_size(md->mdhim_comm, &md_size);

	int rc = meta_init_indices();
	if (rc != 0)
		return -1;

	return 0;

}

/**
* initialize the key and value list used to
* put/get key-value pairs
* ToDo: split once the number of metadata exceeds MAX_META_PER_SEND
*/
int meta_init_indices() {

	int i;

	/*init index metadata*/
	burstfs_keys = (burstfs_key_t **)malloc(MAX_META_PER_SEND\
			* sizeof(burstfs_key_t *));

	burstfs_vals = (burstfs_val_t **)malloc(MAX_META_PER_SEND\
				* sizeof(burstfs_val_t *));

	for (i = 0; i < MAX_META_PER_SEND; i++) {
		burstfs_keys[i] = (burstfs_key_t *)malloc(sizeof(burstfs_key_t));

		if (!burstfs_keys[i])
			return ULFS_ERROR_NOMEM;
		memset(burstfs_keys[i], 0, sizeof(burstfs_key_t));
	}

	for (i = 0; i < MAX_META_PER_SEND; i++) {
		burstfs_vals[i] = (burstfs_val_t *)malloc(sizeof(burstfs_val_t));
		if (!burstfs_vals[i])
			return ULFS_ERROR_NOMEM;;
		memset(burstfs_vals[i], 0, sizeof(burstfs_val_t));
	}

	/*init attribute metadata*/
	fattr_keys = (fattr_key_t **)malloc(MAX_FILE_CNT_PER_NODE \
			* sizeof(fattr_key_t *));

	fattr_vals = (fattr_val_t **)malloc(MAX_FILE_CNT_PER_NODE \
				* sizeof(fattr_val_t *));

	for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
		fattr_keys[i] = (fattr_key_t *)malloc(sizeof(fattr_key_t));

		if (!fattr_keys[i])
			return ULFS_ERROR_NOMEM;
		memset(fattr_keys[i], 0, sizeof(fattr_key_t));
	}

	for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
		fattr_vals[i] = (fattr_val_t *)malloc(sizeof(fattr_val_t));
		if (!fattr_vals[i])
			return ULFS_ERROR_NOMEM;;
		memset(fattr_vals[i], 0, sizeof(fattr_val_t));
	}

	return 0;

}

/**
* store the file attribute to the key-value store
* @param buf: file attribute received from the client
* @param sock_id: the connection id in poll_set of
* the delegator
* @return success/error code
*/
int meta_process_attr_set(char *buf, int sock_id) {
	int rc = ULFS_SUCCESS;

	*fattr_keys[0] = *((int *)(buf + 2 * sizeof(int)));

	burstfs_file_attr_t *ptr_fattr =\
			(burstfs_file_attr_t *)(buf + 3 * sizeof(int));

	fattr_vals[0]->file_attr = ptr_fattr->file_attr;
	strcpy(fattr_vals[0]->fname, ptr_fattr->filename);

/*	LOG(LOG_DBG, "rank:%d, setting fattr key:%d, value:%s\n",\
			glb_rank, *fattr_keys[0], fattr_vals[0]->fname); */
	md->primary_index = burstfs_indexes[1];
	brm = mdhimPut(md, fattr_keys[0], sizeof(fattr_key_t), \
		       fattr_vals[0], sizeof(fattr_val_t), \
		       NULL, NULL);
	if (!brm || brm->error) {
		rc = ULFS_ERROR_MDHIM;
	} else {
		rc = ULFS_SUCCESS;
	}

	mdhim_full_release_msg(brm);

	return rc;
}


/* get the file attribute from the key-value store
* @param buf: a buffer that stores the gid
* @param sock_id: the connection id in poll_set of the delegator
* @return success/error code
*/

int meta_process_attr_get(char *buf, int sock_id,\
		burstfs_file_attr_t *ptr_attr_val) {
	*fattr_keys[0] = *((int *)(buf + 2 * sizeof(int)));
	fattr_val_t *tmp_ptr_attr;

	int rc;

	md->primary_index = burstfs_indexes[1];
	bgrm = mdhimGet(md, md->primary_index, fattr_keys[0],\
			sizeof(fattr_key_t), MDHIM_GET_EQ);

	if (!bgrm || bgrm->error) {
		rc = ULFS_ERROR_MDHIM;
	} else {
		tmp_ptr_attr = (fattr_val_t *)bgrm->values[0];
		ptr_attr_val->gfid = *fattr_keys[0];

	/*	LOG(LOG_DBG, "rank:%d, getting fattr key:%d\n",\
				glb_rank, *fattr_keys[0]); */
		ptr_attr_val->file_attr = tmp_ptr_attr->file_attr;
		strcpy(ptr_attr_val->filename, tmp_ptr_attr->fname);

		rc = ULFS_SUCCESS;
	}

	mdhim_full_release_msg(bgrm);
	return rc;
}

/*synchronize all the indices and file attributes
* to the key-value store
* @param sock_id: the connection id in poll_set of the delegator
* @return success/error code
*/

int meta_process_fsync (int sock_id) {
	int ret = 0;

	int app_id = invert_sock_ids[sock_id];
	app_config_t *app_config = (app_config_t *)arraylist_get(app_config_list, app_id);

	int client_side_id = app_config->client_ranks[sock_id];

	unsigned long num_entries =\
			*((unsigned long *)(app_config->shm_superblocks[client_side_id]\
			+ app_config->meta_offset));

	/* indices are stored in the superblock shared memory
	 *  created by the client*/
	int page_sz = getpagesize();
	burstfs_index_t *meta_payload =\
			(burstfs_index_t *)(app_config->shm_superblocks[client_side_id]\
			+ app_config->meta_offset + page_sz);

	md->primary_index = burstfs_indexes[0];

	long i;
	for (i = 0; i < num_entries; i++) {
		burstfs_keys[i]->fid = meta_payload[i].fid;
		burstfs_keys[i]->offset = meta_payload[i].file_pos;
		burstfs_vals[i]->addr = meta_payload[i].mem_pos;
		burstfs_vals[i]->len = meta_payload[i].length;
		burstfs_vals[i]->delegator_id = glb_rank;
		memcpy((char *)&(burstfs_vals[i]->app_rank_id), &app_id, sizeof(int));
		memcpy((char  *)&(burstfs_vals[i]->app_rank_id) + sizeof(int),\
				&client_side_id, sizeof(int));

		burstfs_key_lens[i] = sizeof(burstfs_key_t);
		burstfs_val_lens[i] = sizeof(burstfs_val_t);
	}

   // print_fsync_indices(burstfs_keys, burstfs_vals, num_entries);

	brm = mdhimBPut(md, (void **)(&burstfs_keys[0]), burstfs_key_lens,
			(void **)(&burstfs_vals[0]), burstfs_val_lens, num_entries,
			NULL, NULL);
	brmp = brm;
	if (!brmp || brmp->error) {
		  ret = ULFS_ERROR_MDHIM;
		  LOG(LOG_DBG, "Rank - %d: Error inserting keys/values into MDHIM\n",\
				  md->mdhim_rank);
	}

	while (brmp) {
		if (brmp->error < 0) {
			ret = ULFS_ERROR_MDHIM;
			break;
		}

		brm = brmp;
		brmp = brmp->next;
		mdhim_full_release_msg(brm);

	}

	md->primary_index = burstfs_indexes[1];

	num_entries =\
			*((unsigned long *)(app_config->shm_superblocks[client_side_id]\
					+ app_config->fmeta_offset));

	/* file attributes are stored in the superblock shared memory
	 * created by the client*/
	burstfs_file_attr_t *attr_payload =\
			(burstfs_file_attr_t *)(app_config->shm_superblocks[client_side_id]\
					+ app_config->fmeta_offset + page_sz);


	for (i = 0; i < num_entries; i++) {
		*fattr_keys[i] = attr_payload[i].gfid;
		fattr_vals[i]->file_attr = attr_payload[i].file_attr;
		strcpy(fattr_vals[i]->fname, attr_payload[i].filename);

		fattr_key_lens[i] = sizeof(fattr_key_t);
		fattr_val_lens[i] = sizeof(fattr_val_t);
	}

	brm = mdhimBPut(md, (void **)(&fattr_keys[0]), fattr_key_lens,
			(void **)(&fattr_vals[0]), fattr_val_lens, num_entries,
			NULL, NULL);
	brmp = brm;
	if (!brmp || brmp->error) {
		  ret = ULFS_ERROR_MDHIM;
		  LOG(LOG_DBG, "Rank - %d: Error inserting keys/values into MDHIM\n",\
				  md->mdhim_rank);
	}

	while (brmp) {
		if (brmp->error < 0) {
			ret = ULFS_ERROR_MDHIM;
			break;
		}

		brm = brmp;
		brmp = brmp->next;
		mdhim_full_release_msg(brm);

	}

	return ret;
}


/* get the locations of all the requested file segments from
 * the key-value store.
* @param app_id: client's application id
* @param client_id: client-side process id
* @param del_req_set: the set of read requests to be
* @param thrd_id: the thread created for processing
* its client's read requests.
* @param dbg_rank: the client process's rank in its
* own application, used for debug purpose
* @param shm_reqbuf: the shared request memory that
* contains all the client's read requests
* @del_req_set: contains metadata information for all
* the read requests, such as the locations of the
* requested segments
* @return success/error code
*/
int meta_batch_get(int app_id, int client_id,\
		int thrd_id, int dbg_rank, char *shm_reqbuf, int num,\
			msg_meta_t* del_req_set) {
	cli_req_t *tmp_cli_req = (cli_req_t *) shm_reqbuf;

	int i, rc = 0;
	for (i = 0; i < num; i++) {
		burstfs_keys[2 * i]->fid = tmp_cli_req[i].fid;
		burstfs_keys[2 * i]->offset = tmp_cli_req[i].offset;
		burstfs_key_lens[2 * i] = sizeof(burstfs_key_t);
		burstfs_keys[2 * i + 1]->fid = tmp_cli_req[i].fid;
		burstfs_keys[2 * i + 1]->offset =\
				tmp_cli_req[i].offset + tmp_cli_req[i].length - 1;
		burstfs_key_lens[2 * i + 1] = sizeof(burstfs_key_t);

	}

	md->primary_index = burstfs_indexes[0];
	bgrm = mdhimBGet(md, md->primary_index, burstfs_keys, burstfs_key_lens, \
			2 * num, MDHIM_RANGE_BGET);

	int tot_num = 0;
    int dest_client, dest_app;
    burstfs_key_t *tmp_key;
    burstfs_val_t *tmp_val;

	bgrmp = bgrm;
	while (bgrmp) {
	  if (bgrmp->error < 0) {
		  	 rc = ULFS_ERROR_MDHIM;
	  }

	  for (i = 0; i < bgrmp->num_keys; i++) {
		  tmp_key = (burstfs_key_t *)bgrm->keys[i];
		  tmp_val = (burstfs_val_t *)bgrm->values[i];

		  memcpy(&dest_app, (char *)&(tmp_val->app_rank_id), sizeof(int));
		  memcpy(&dest_client, (char *)&(tmp_val->app_rank_id)\
				  + sizeof(int), sizeof(int));
		  /* physical offset of the requested file segment on the log file*/
		  del_req_set->msg_meta[tot_num].dest_offset = tmp_val->addr;

		  /* rank of the remote delegator*/
		  del_req_set->msg_meta[tot_num].dest_delegator_rank = tmp_val->delegator_id;

		  /* dest_client_id and dest_app_id uniquely identifies the remote physical
		   * log file that contains the requested segments*/
		  del_req_set->msg_meta[tot_num].dest_client_id = dest_client;
		  del_req_set->msg_meta[tot_num].dest_app_id = dest_app;
		  del_req_set->msg_meta[tot_num].length = tmp_val->len;

		  /* src_app_id and src_cli_id identifies the requested client*/
		  del_req_set->msg_meta[tot_num].src_app_id = app_id;
		  del_req_set->msg_meta[tot_num].src_cli_id = client_id;

		  /* src_offset is the logical offset of the shared file*/
		  del_req_set->msg_meta[tot_num].src_offset = tmp_key->offset;
		  del_req_set->msg_meta[tot_num].src_delegator_rank = glb_rank;
		  del_req_set->msg_meta[tot_num].src_fid = tmp_key->fid;
		  del_req_set->msg_meta[tot_num].src_dbg_rank = dbg_rank;
		  del_req_set->msg_meta[tot_num].src_thrd = thrd_id;
		  tot_num++;
	  	}
		  bgrmp = bgrmp->next;
		  mdhim_full_release_msg(bgrm);
		  bgrm = bgrmp;
    }

	del_req_set->num = tot_num;
//    print_bget_indices(app_id, client_id, del_req_set->msg_meta, tot_num);

	return rc;
}

void print_bget_indices(int app_id, int cli_id, \
		send_msg_t *index_set, int tot_num) {
	int i;

	long dest_offset;
	int dest_delegator_rank;
	int dest_client_id;
	int dest_app_id;
	long length;
	int src_app_id;
	int src_cli_id;
	long src_offset;
	int src_delegator_rank;
	int src_fid;
	int dbg_rank;

//	LOG(LOG_DBG, "indices: app_id:%d, cli_id:%d, tot_num:%d\n",\
			app_id, cli_id, tot_num);
	for (i = 0; i < tot_num;  i++) {
		  dest_offset = index_set[i].dest_offset;
		  dest_delegator_rank = index_set[i].dest_delegator_rank;
		  dest_client_id = index_set[i].dest_client_id;
		  dest_app_id = index_set[i].dest_app_id;
		  length = index_set[i].length;
		  src_app_id = index_set[i].src_app_id;
		  src_cli_id = index_set[i].src_cli_id;
		  src_offset = index_set[i].src_offset;

		  src_delegator_rank = index_set[i].src_delegator_rank;
		  src_fid = index_set[i].src_fid;
		  dbg_rank = index_set[i].src_dbg_rank;

		  LOG(LOG_DBG, "index:dbg_rank:%d, dest_offset:%ld,\
				   dest_del_rank:%d, dest_cli_id:%d, dest_app_id:%d, \	
				   length:%ld, src_app_id:%d, src_cli_id:%d, src_offset:%ld,\
				   	   src_del_rank:%d, \
					   	   src_fid:%d, num:%ld\n", dbg_rank, dest_offset,\
					   			    dest_delegator_rank, dest_client_id, \
				                         dest_app_id, length, src_app_id, src_cli_id,\
					   			   		 src_offset, src_delegator_rank, 
						   	   	   	   	   src_fid, tot_num);

	}


}

void print_fsync_indices(burstfs_key_t **burstfs_keys,\
		burstfs_val_t **burstfs_vals, long num_entries) {
	long i;
	for (i = 0; i < num_entries; i++) {
		LOG(LOG_DBG, "fid:%ld, offset:%ld, addr:%ld, len:%ld, del_id:%ld\n", \
				burstfs_keys[i]->fid, burstfs_keys[i]->offset, \
					burstfs_vals[i]->addr, burstfs_vals[i]->len, \
						burstfs_vals[i]->delegator_id);

	}
}

int meta_free_indices() {
	int i;
	for (i = 0; i < MAX_META_PER_SEND; i++) {
		free(burstfs_keys[i]);
	}
	free(burstfs_keys);

	for (i = 0; i < MAX_META_PER_SEND; i++) {
		free(burstfs_vals[i]);
	}
	free(burstfs_vals);

	for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
		free(fattr_keys[i]);
	}
	free(fattr_keys);

	for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
		free(fattr_vals[i]);
	}
	free(fattr_vals);
	return 0;
}

int meta_sanitize() {
	int rc = ULFS_SUCCESS;

	meta_free_indices();

	char dbfilename[GEN_STR_LEN] = {0};
	char statfilename[GEN_STR_LEN] = {0};
	char manifestname[GEN_STR_LEN] = {0};

	char dbfilename1[GEN_STR_LEN] = {0};
	char statfilename1[GEN_STR_LEN] = {0};
	char manifestname1[GEN_STR_LEN] = {0};
	sprintf(dbfilename, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name,
			burstfs_indexes[0]->id, md->mdhim_rank);

	sprintf(statfilename, "%s_stats", dbfilename);
	sprintf(manifestname, "%s%d_%d_%d", md->db_opts->manifest_path,\
			burstfs_indexes[0]->type,
				burstfs_indexes[0]->id, md->mdhim_rank);

	sprintf(dbfilename1, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name,
			burstfs_indexes[1]->id, md->mdhim_rank);

	sprintf(statfilename1, "%s_stats", dbfilename1);
	sprintf(manifestname1, "%s%d_%d_%d", md->db_opts->manifest_path,\
				burstfs_indexes[1]->type,
					burstfs_indexes[1]->id, md->mdhim_rank);

	mdhimClose(md);
	rc = mdhimSanitize(dbfilename, statfilename, manifestname);
	rc = mdhimSanitize(dbfilename1, statfilename1, manifestname1);

	mdhim_options_destroy(db_opts);
	return rc;
}
