/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Wekuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICNSE for full license text.
 */
#include <assert.h>
#include "mdhim.h"
#include "indexes.h"
#include "log.h"
#include "unifycr_meta.h"
#include "unifycr_metadata.h"
#include "arraylist.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "../../client/src/ucr_read_builder.h"

unifycr_key_t **unifycr_keys;
unifycr_val_t **unifycr_vals;

fattr_key_t **fattr_keys;
fattr_val_t **fattr_vals;

char *manifest_path;

struct mdhim_brm_t *brm, *brmp;
struct mdhim_bgetrm_t *bgrm, *bgrmp;

mdhim_options_t *db_opts;
struct mdhim_t *md;

int md_size;
int unifycr_key_lens[MAX_META_PER_SEND] = {0};
int unifycr_val_lens[MAX_META_PER_SEND] = {0};

int fattr_key_lens[MAX_FILE_CNT_PER_NODE] = {0};
int fattr_val_lens[MAX_FILE_CNT_PER_NODE] = {0};

struct index_t *unifycr_indexes[2];
long max_recs_per_slice;
/**
* initialize the key-value store
*/
int meta_init_store(unifycr_cfg_t *cfg)
{
    int rc, ser_ratio;
    size_t path_len;
    long svr_ratio, range_sz;
    MPI_Comm comm = MPI_COMM_WORLD;

    if (cfg == NULL)
        return -1;

    db_opts = calloc(1, sizeof(struct mdhim_options_t));
    if (db_opts == NULL)
        return -1;

    /* UNIFYCR_META_DB_PATH: file that stores the key value pair*/
    db_opts->db_path = strdup(cfg->meta_db_path);
    if (db_opts->db_path == NULL)
        return -1;

    db_opts->manifest_path = NULL;
    db_opts->db_type = LEVELDB;
    db_opts->db_create_new = 1;

    /* META_SERVER_RATIO: number of metadata servers =
        number of processes/META_SERVER_RATIO */
    svr_ratio = 0;
    rc = configurator_int_val(cfg->meta_server_ratio, &svr_ratio);
    if (rc != 0)
        return -1;
    ser_ratio = (int) svr_ratio;
    db_opts->rserver_factor = ser_ratio;

    db_opts->db_paths = NULL;
    db_opts->num_paths = 0;
    db_opts->num_wthreads = 1;

    path_len = strlen(db_opts->db_path) + strlen(MANIFEST_FILE_NAME) + 1;
    manifest_path = malloc(path_len);
    if (manifest_path == NULL)
        return -1;
    sprintf(manifest_path, "%s/%s", db_opts->db_path, MANIFEST_FILE_NAME);
    db_opts->manifest_path = manifest_path;

    db_opts->db_name = strdup(cfg->meta_db_name);
    if (db_opts->db_name == NULL)
        return -1;

    db_opts->db_key_type = MDHIM_UNIFYCR_KEY;
    db_opts->debug_level = MLOG_CRIT;

    /* indices/attributes are striped to servers according
     * to UnifyCR_META_RANGE_SIZE.
     */
    range_sz = 0;
    rc = configurator_int_val(cfg->meta_range_size, &range_sz);
    if (rc != 0)
        return -1;
    max_recs_per_slice = range_sz;
    db_opts->max_recs_per_slice = (uint64_t) range_sz;

    md = mdhimInit(&comm, db_opts);

    /*this index is created for storing index metadata*/
    unifycr_indexes[0] = md->primary_index;

    /*this index is created for storing file attribute metadata*/
    unifycr_indexes[1] = create_global_index(md, ser_ratio, 1,
                         LEVELDB, MDHIM_INT_KEY, "file_attr");

    MPI_Comm_size(md->mdhim_comm, &md_size);

    rc = meta_init_indices();
    if (rc != 0)
        return -1;

    return 0;

}

/**
* initialize the key and value list used to
* put/get key-value pairs
* ToDo: split once the number of metadata exceeds MAX_META_PER_SEND
*/
int meta_init_indices()
{

    int i;

    /*init index metadata*/
    unifycr_keys = (unifycr_key_t **)malloc(MAX_META_PER_SEND
                                            * sizeof(unifycr_key_t *));

    unifycr_vals = (unifycr_val_t **)malloc(MAX_META_PER_SEND
                                            * sizeof(unifycr_val_t *));

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifycr_keys[i] = (unifycr_key_t *)malloc(sizeof(unifycr_key_t));
        if (unifycr_keys[i] == NULL)
            return (int)UNIFYCR_ERROR_NOMEM;
        memset(unifycr_keys[i], 0, sizeof(unifycr_key_t));
    }

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifycr_vals[i] = (unifycr_val_t *)malloc(sizeof(unifycr_val_t));
        if (unifycr_vals[i] == NULL)
            return (int)UNIFYCR_ERROR_NOMEM;
        memset(unifycr_vals[i], 0, sizeof(unifycr_val_t));
    }

    /*init attribute metadata*/
    fattr_keys = (fattr_key_t **)malloc(MAX_FILE_CNT_PER_NODE
                                        * sizeof(fattr_key_t *));

    fattr_vals = (fattr_val_t **)malloc(MAX_FILE_CNT_PER_NODE
                                        * sizeof(fattr_val_t *));

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_keys[i] = (fattr_key_t *)malloc(sizeof(fattr_key_t));
        if (fattr_keys[i] == NULL)
            return (int)UNIFYCR_ERROR_NOMEM;
        memset(fattr_keys[i], 0, sizeof(fattr_key_t));
    }

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_vals[i] = (fattr_val_t *)malloc(sizeof(fattr_val_t));
        if (fattr_vals[i] == NULL)
            return (int)UNIFYCR_ERROR_NOMEM;
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
int meta_process_attr_set(int gfid, const char* filename)
{
    int rc = ULFS_SUCCESS;

    fattr_val_t fattr_vals_local;
    memset(&fattr_vals_local, 0, sizeof(fattr_val_t));

    strcpy(fattr_vals_local.fname, filename);

    printf("Setting Attribute -- rank:%d, setting fattr key:%d, value:%s\n",
                glb_rank, gfid, fattr_vals_local.fname);
    md->primary_index = unifycr_indexes[1];
    brm = mdhimPut(md, &gfid, sizeof(fattr_key_t),
                   &fattr_vals_local, sizeof(fattr_val_t),
                   NULL, NULL);
    if (!brm || brm->error)
        rc = (int)UNIFYCR_ERROR_MDHIM;
    else
        rc = ULFS_SUCCESS;

    mdhim_full_release_msg(brm);

    return rc;
}


/* get the file attribute from the key-value store
* @param gfid: the global file ID
* @return success/error code
*/

int meta_process_attr_get(int gfid, unifycr_file_attr_t* ptr_attr_val)
{
    fattr_key_t fattr_keys_local = gfid;
    fattr_val_t *tmp_ptr_attr;

    int rc;

    md->primary_index = unifycr_indexes[1];
    bgrm = mdhimGet(md, md->primary_index, &fattr_keys_local,
                    sizeof(fattr_key_t), MDHIM_GET_EQ);

    if (!bgrm || bgrm->error)
        rc = (int)UNIFYCR_ERROR_MDHIM;
    else {
        tmp_ptr_attr = (fattr_val_t *)bgrm->values[0];
        ptr_attr_val->gfid = fattr_keys_local;

        printf("getting File Attribute: rank:%d, getting fattr key:%d\n",
                    glb_rank, fattr_keys_local); 
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

int meta_process_fsync(int app_id, int client_side_id, int gfid)
{
    int ret = 0;

    app_config_t *app_config = (app_config_t *)arraylist_get(app_config_list,
                               app_id);

    // int client_side_id = app_config->client_ranks[sock_id];

    unsigned long num_entries =
        *((unsigned long *)(app_config->shm_superblocks[client_side_id]
                            + app_config->meta_offset));

    /* indices are stored in the superblock shared memory
     *  created by the client*/
    int page_sz = getpagesize();
    unifycr_index_t *meta_payload =
        (unifycr_index_t *)(app_config->shm_superblocks[client_side_id]
                            + app_config->meta_offset + page_sz);

    md->primary_index = unifycr_indexes[0];

    int used_entries = 0;
    int i;
    for (i = 0; i < num_entries; i++) {
        if ( meta_payload[i].fid == gfid ) {
			printf("setting mdata for gfid: %d, file_pos: %lu, mem_pos: %lu, length: %lu\n", meta_payload[i].fid, meta_payload[i].file_pos, meta_payload[i].mem_pos, meta_payload[i].length); 
            unifycr_keys[used_entries]->fid = meta_payload[i].fid;
            unifycr_keys[used_entries]->offset = meta_payload[i].file_pos;
            unifycr_vals[used_entries]->addr = meta_payload[i].mem_pos;
            unifycr_vals[used_entries]->len = meta_payload[i].length;
            unifycr_vals[used_entries]->delegator_id = glb_rank;
            memcpy((char *) & (unifycr_vals[i]->app_rank_id), &app_id, sizeof(int));
            memcpy((char *) & (unifycr_vals[i]->app_rank_id) + sizeof(int),
                &client_side_id, sizeof(int));
    
            unifycr_key_lens[used_entries] = sizeof(unifycr_key_t);
            unifycr_val_lens[used_entries] = sizeof(unifycr_val_t);
            used_entries++;
        }
    }

    // print_fsync_indices(unifycr_keys, unifycr_vals, num_entries);

    brm = mdhimBPut(md, (void **)(&unifycr_keys[0]), unifycr_key_lens,
                    (void **)(&unifycr_vals[0]), unifycr_val_lens, used_entries,
                    NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        ret = (int)UNIFYCR_ERROR_MDHIM;
        LOG(LOG_DBG, "Rank - %d: Error inserting keys/values into MDHIM\n",
            md->mdhim_rank);
    }

    while (brmp) {
        if (brmp->error < 0) {
            ret = (int)UNIFYCR_ERROR_MDHIM;
            break;
        }

        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);

    }

    md->primary_index = unifycr_indexes[1];

    num_entries =
        *((unsigned long *)(app_config->shm_superblocks[client_side_id]
                            + app_config->fmeta_offset));

    /* file attributes are stored in the superblock shared memory
     * created by the client*/
    unifycr_file_attr_t *attr_payload =
        (unifycr_file_attr_t *)(app_config->shm_superblocks[client_side_id]
                                + app_config->fmeta_offset + page_sz);


    used_entries = 0;
    for (i = 0; i < num_entries; i++) {
        if ( attr_payload[i].gfid == gfid ) {
            *fattr_keys[used_entries] = attr_payload[i].gfid;
            fattr_vals[used_entries]->file_attr = attr_payload[i].file_attr;
            strcpy(fattr_vals[used_entries]->fname, attr_payload[i].filename);
    
            fattr_key_lens[used_entries] = sizeof(fattr_key_t);
            fattr_val_lens[used_entries] = sizeof(fattr_val_t);
            used_entries++;
        }
    }
    assert(used_entries == 1);

    // should be changed to mdhimPut once assertion is validated
    brm = mdhimBPut(md, (void **)(&fattr_keys[0]), fattr_key_lens,
                    (void **)(&fattr_vals[0]), fattr_val_lens, used_entries,
                    NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        ret = (int)UNIFYCR_ERROR_MDHIM;
        LOG(LOG_DBG, "Rank - %d: Error inserting keys/values into MDHIM\n",
            md->mdhim_rank);
    }

    while (brmp) {
        if (brmp->error < 0) {
            ret = (int)UNIFYCR_ERROR_MDHIM;
            break;
        }

        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);

    }

    return ret;
}

int meta_read_get(int app_id, int client_id,
                   int thrd_id, int dbg_rank, int gfid, long offset, long length,
                   msg_meta_t *del_req_set)
{
    /* assume we'll succeed, set to error otherwise */
    int rc = (int)UNIFYCR_SUCCESS;

    printf("in meta_read_get for app_id: %d, client_id: %d, thrd_id: %d\n", app_id, client_id, thrd_id);
    printf("fid: %d, offset: %d, length: %d\n", gfid, offset, length);

    unifycr_keys[0]->fid = gfid;
    unifycr_keys[0]->offset = offset;
    unifycr_key_lens[0] = sizeof(unifycr_key_t);
    unifycr_keys[1]->fid = gfid;
    unifycr_keys[1]->offset = offset + length -1;
    unifycr_key_lens[1] = sizeof(unifycr_key_t);

    md->primary_index = unifycr_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void **)unifycr_keys,
                     unifycr_key_lens, 2, MDHIM_RANGE_BGET);

    int tot_num = 0;
    int dest_client, dest_app;
    unifycr_key_t *tmp_key;
    unifycr_val_t *tmp_val;

    bgrmp = bgrm;
    while (bgrmp) {
        if (bgrmp->error < 0) {
            rc = (int)UNIFYCR_ERROR_MDHIM;
        }
		int i;
        for (i = 0; i < bgrmp->num_keys; i++) {
            tmp_key = (unifycr_key_t *)bgrm->keys[i];
            tmp_val = (unifycr_val_t *)bgrm->values[i];

            memcpy(&dest_app, (char *) & (tmp_val->app_rank_id), sizeof(int));
            memcpy(&dest_client, (char *) & (tmp_val->app_rank_id)
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

/* get the locations of all the requested file segments from
 * the key-value store.
* @param app_id: client's application id
* @param client_id: client-side iiiikkijuprocess id
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
int meta_batch_get(int app_id, int client_id,
                   int thrd_id, int dbg_rank, void* reqbuf, int num,
                   msg_meta_t *del_req_set)
{
	printf("in meta_batch_get for app_id: %d, client_id: %d, thrd_id: %d\n", app_id, client_id, thrd_id);
    //cli_req_t *tmp_cli_req = (cli_req_t *) shm_reqbuf;
    unifycr_ReadRequest_table_t readRequest = unifycr_ReadRequest_as_root(reqbuf);
	unifycr_Extent_vec_t extents = unifycr_ReadRequest_extents(readRequest);
	size_t extents_len = unifycr_Extent_vec_len(extents);
	assert(extents_len == num);
    int i, rc = 0;
    for (i = 0; i < num; i++) {
		printf("fid: %d, offset: %d, length: %d\n",
                unifycr_Extent_fid(unifycr_Extent_vec_at(extents,i)),
            unifycr_Extent_offset(unifycr_Extent_vec_at(extents,i)),
			unifycr_Extent_length(unifycr_Extent_vec_at(extents,i)));
        unifycr_keys[2 * i]->fid = unifycr_Extent_fid(unifycr_Extent_vec_at(extents,i));
        unifycr_keys[2 * i]->offset =
			unifycr_Extent_offset(unifycr_Extent_vec_at(extents,i));
        unifycr_key_lens[2 * i] = sizeof(unifycr_key_t);
        unifycr_keys[2 * i + 1]->fid =
			unifycr_Extent_fid(unifycr_Extent_vec_at(extents,i));
        unifycr_keys[2 * i + 1]->offset =
			unifycr_Extent_offset(unifycr_Extent_vec_at(extents,i)) + unifycr_Extent_length(unifycr_Extent_vec_at(extents,i)) - 1;
        unifycr_key_lens[2 * i + 1] = sizeof(unifycr_key_t);
    }

    md->primary_index = unifycr_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void **)unifycr_keys,
                     unifycr_key_lens, 2 * num, MDHIM_RANGE_BGET);

    int tot_num = 0;
    int dest_client, dest_app;
    unifycr_key_t *tmp_key;
    unifycr_val_t *tmp_val;

    bgrmp = bgrm;
    while (bgrmp) {
        if (bgrmp->error < 0)
            rc = (int)UNIFYCR_ERROR_MDHIM;

        for (i = 0; i < bgrmp->num_keys; i++) {
            tmp_key = (unifycr_key_t *)bgrm->keys[i];
            tmp_val = (unifycr_val_t *)bgrm->values[i];

            memcpy(&dest_app, (char *) & (tmp_val->app_rank_id), sizeof(int));
            memcpy(&dest_client, (char *) & (tmp_val->app_rank_id)
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

void print_bget_indices(int app_id, int cli_id,
                        send_msg_t *index_set, int tot_num)
{
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

        LOG(LOG_DBG, "index:dbg_rank:%d, dest_offset:%ld, "
            "dest_del_rank:%d, dest_cli_id:%d, dest_app_id:%d, "
            "length:%ld, src_app_id:%d, src_cli_id:%d, src_offset:%ld, "
            "src_del_rank:%d, "
            "src_fid:%d, num:%d\n", dbg_rank, dest_offset,
            dest_delegator_rank, dest_client_id,
            dest_app_id, length, src_app_id, src_cli_id,
            src_offset, src_delegator_rank,
            src_fid, tot_num);

    }


}

void print_fsync_indices(unifycr_key_t **unifycr_keys,
                         unifycr_val_t **unifycr_vals, long num_entries)
{
    long i;
    for (i = 0; i < num_entries; i++) {
        LOG(LOG_DBG, "fid:%ld, offset:%ld, addr:%ld, len:%ld, del_id:%ld\n",
            unifycr_keys[i]->fid, unifycr_keys[i]->offset,
            unifycr_vals[i]->addr, unifycr_vals[i]->len,
            unifycr_vals[i]->delegator_id);

    }
}

int meta_free_indices()
{
    int i;
    for (i = 0; i < MAX_META_PER_SEND; i++)
        free(unifycr_keys[i]);
    free(unifycr_keys);

    for (i = 0; i < MAX_META_PER_SEND; i++)
        free(unifycr_vals[i]);
    free(unifycr_vals);

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++)
        free(fattr_keys[i]);
    free(fattr_keys);

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++)
        free(fattr_vals[i]);
    free(fattr_vals);

    return 0;
}

int meta_sanitize()
{
    int rc = ULFS_SUCCESS;

    meta_free_indices();

    char dbfilename[UNIFYCR_MAX_FILENAME] = {0};
    char statfilename[UNIFYCR_MAX_FILENAME] = {0};
    char manifestname[UNIFYCR_MAX_FILENAME] = {0};

    char dbfilename1[UNIFYCR_MAX_FILENAME] = {0};
    char statfilename1[UNIFYCR_MAX_FILENAME] = {0};
    char manifestname1[UNIFYCR_MAX_FILENAME] = {0};
    sprintf(dbfilename, "%s/%s-%d-%d", md->db_opts->db_path,
            md->db_opts->db_name, unifycr_indexes[0]->id, md->mdhim_rank);

    sprintf(statfilename, "%s_stats", dbfilename);
    sprintf(manifestname, "%s%d_%d_%d", md->db_opts->manifest_path,
            unifycr_indexes[0]->type,
            unifycr_indexes[0]->id, md->mdhim_rank);

    sprintf(dbfilename1, "%s/%s-%d-%d", md->db_opts->db_path,
            md->db_opts->db_name, unifycr_indexes[1]->id, md->mdhim_rank);

    sprintf(statfilename1, "%s_stats", dbfilename1);
    sprintf(manifestname1, "%s%d_%d_%d", md->db_opts->manifest_path,
            unifycr_indexes[1]->type,
            unifycr_indexes[1]->id, md->mdhim_rank);

    mdhimClose(md);
    rc = mdhimSanitize(dbfilename, statfilename, manifestname);
    rc = mdhimSanitize(dbfilename1, statfilename1, manifestname1);

    mdhim_options_destroy(db_opts);
    return rc;
}
