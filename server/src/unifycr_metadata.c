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
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "arraylist.h"
#include "indexes.h"
#include "mdhim.h"
#include "unifycr_log.h"
#include "unifycr_metadata.h"
#include "unifycr_clientcalls_rpc.h"
#include "ucr_read_builder.h"

unifycr_key_t** unifycr_keys;
unifycr_val_t** unifycr_vals;

fattr_key_t** fattr_keys;
fattr_val_t** fattr_vals;

char* manifest_path;

struct mdhim_brm_t* brm, *brmp;
struct mdhim_bgetrm_t* bgrm, *bgrmp;

mdhim_options_t* db_opts;
struct mdhim_t* md;

int md_size;
int unifycr_key_lens[MAX_META_PER_SEND] = {0};
int unifycr_val_lens[MAX_META_PER_SEND] = {0};

int fattr_key_lens[MAX_FILE_CNT_PER_NODE] = {0};
int fattr_val_lens[MAX_FILE_CNT_PER_NODE] = {0};

struct index_t* unifycr_indexes[2];
size_t max_recs_per_slice;

void debug_log_key_val(const char* ctx,
                       unifycr_key_t* key,
                       unifycr_val_t* val)
{
    if ((key != NULL) && (val != NULL)) {
        LOGDBG("@%s - key(fid=%lu, offset=%lu), "
               "val(del=%d, len=%lu, addr=%lu, app=%d, rank=%d)",
               ctx, key->fid, key->offset,
               val->delegator_id, val->len, val->addr, val->app_id, val->rank);
    } else if (key != NULL) {
        LOGDBG("@%s - key(fid=%lu, offset=%lu)",
               ctx, key->fid, key->offset);
    }
}

int unifycr_key_compare(unifycr_key_t* a, unifycr_key_t* b)
{
    assert((NULL != a) && (NULL != b));
    if (a->fid == b->fid) {
        if (a->offset == b->offset) {
            return 0;
        } else if (a->offset < b->offset) {
            return -1;
        } else {
            return 1;
        }
    } else if (a->fid < b->fid) {
        return -1;
    } else {
        return 1;
    }
}

/**
* initialize the key-value store
*/
int meta_init_store(unifycr_cfg_t* cfg)
{
    int rc, ser_ratio;
    size_t path_len;
    long svr_ratio, range_sz;
    MPI_Comm comm = MPI_COMM_WORLD;

    if (cfg == NULL) {
        return -1;
    }

    db_opts = calloc(1, sizeof(struct mdhim_options_t));
    if (db_opts == NULL) {
        return -1;
    }

    /* UNIFYCR_META_DB_PATH: file that stores the key value pair*/
    db_opts->db_path = strdup(cfg->meta_db_path);
    if (db_opts->db_path == NULL) {
        return -1;
    }

    db_opts->manifest_path = NULL;
    db_opts->db_type = LEVELDB;
    db_opts->db_create_new = 1;

    /* META_SERVER_RATIO: number of metadata servers =
        number of processes/META_SERVER_RATIO */
    svr_ratio = 0;
    rc = configurator_int_val(cfg->meta_server_ratio, &svr_ratio);
    if (rc != 0) {
        return -1;
    }
    ser_ratio = (int) svr_ratio;
    db_opts->rserver_factor = ser_ratio;

    db_opts->db_paths = NULL;
    db_opts->num_paths = 0;
    db_opts->num_wthreads = 1;

    path_len = strlen(db_opts->db_path) + strlen(MANIFEST_FILE_NAME) + 1;
    manifest_path = malloc(path_len);
    if (manifest_path == NULL) {
        return -1;
    }
    sprintf(manifest_path, "%s/%s", db_opts->db_path, MANIFEST_FILE_NAME);
    db_opts->manifest_path = manifest_path;

    db_opts->db_name = strdup(cfg->meta_db_name);
    if (db_opts->db_name == NULL) {
        return -1;
    }

    db_opts->db_key_type = MDHIM_UNIFYCR_KEY;
    db_opts->debug_level = MLOG_CRIT;

    /* indices/attributes are striped to servers according
     * to UnifyCR_META_RANGE_SIZE.
     */
    range_sz = 0;
    rc = configurator_int_val(cfg->meta_range_size, &range_sz);
    if (rc != 0) {
        return -1;
    }
    max_recs_per_slice = (size_t) range_sz;
    db_opts->max_recs_per_slice = (uint64_t) range_sz;

    md = mdhimInit(&comm, db_opts);

    /*this index is created for storing index metadata*/
    unifycr_indexes[0] = md->primary_index;

    /*this index is created for storing file attribute metadata*/
    unifycr_indexes[1] = create_global_index(md, ser_ratio, 1,
                         LEVELDB, MDHIM_INT_KEY, "file_attr");

    MPI_Comm_size(md->mdhim_comm, &md_size);

    rc = meta_init_indices();
    if (rc != 0) {
        return -1;
    }

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
    unifycr_keys = (unifycr_key_t**)malloc(MAX_META_PER_SEND
                                           * sizeof(unifycr_key_t*));

    unifycr_vals = (unifycr_val_t**)malloc(MAX_META_PER_SEND
                                           * sizeof(unifycr_val_t*));

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifycr_keys[i] = (unifycr_key_t*)malloc(sizeof(unifycr_key_t));
        if (unifycr_keys[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
        memset(unifycr_keys[i], 0, sizeof(unifycr_key_t));
    }

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifycr_vals[i] = (unifycr_val_t*)malloc(sizeof(unifycr_val_t));
        if (unifycr_vals[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
        memset(unifycr_vals[i], 0, sizeof(unifycr_val_t));
    }

    /*init attribute metadata*/
    fattr_keys = (fattr_key_t**)malloc(MAX_FILE_CNT_PER_NODE
                                       * sizeof(fattr_key_t*));

    fattr_vals = (fattr_val_t**)malloc(MAX_FILE_CNT_PER_NODE
                                       * sizeof(fattr_val_t*));

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_keys[i] = (fattr_key_t*)malloc(sizeof(fattr_key_t));
        if (fattr_keys[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
        memset(fattr_keys[i], 0, sizeof(fattr_key_t));
    }

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_vals[i] = (fattr_val_t*)malloc(sizeof(fattr_val_t));
        if (fattr_vals[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
        memset(fattr_vals[i], 0, sizeof(fattr_val_t));
    }

    return 0;
}

/**
* store the file attribute to the key-value store
* @param ptr_fattr: file attribute received from the client
* @return success/error code
*/
int meta_process_attr_set(unifycr_file_attr_t* ptr_fattr)
{
    int rc;
    fattr_key_t local_key;
    fattr_val_t local_val;

    assert(NULL != ptr_fattr);

    local_key = (fattr_key_t) ptr_fattr->gfid;
    memset(&local_val, 0, sizeof(local_val));
    strncpy(local_val.fname, ptr_fattr->filename, sizeof(local_val.fname));
    local_val.file_attr = ptr_fattr->file_attr;

    /* LOGDBG("rank:%d, setting fattr key:%d, value:%s",
     *        glb_rank, local_key, local_val.fname); */
    md->primary_index = unifycr_indexes[1];
    brm = mdhimPut(md, &local_key, sizeof(fattr_key_t),
                   &local_val, sizeof(fattr_val_t),
                   NULL, NULL);
    if (!brm || brm->error) {
        rc = (int)UNIFYCR_ERROR_MDHIM;
    } else {
        rc = UNIFYCR_SUCCESS;
    }

    mdhim_full_release_msg(brm);

    return rc;
}


/* get the file attribute from the key-value store
* @param ptr_fattr: file attribute to return to the client
* @return success/error code
*/

int meta_process_attr_get(unifycr_file_attr_t* ptr_fattr)
{
    int rc;

    assert(NULL != ptr_fattr);

    fattr_key_t local_key = (fattr_key_t) ptr_fattr->gfid;

    md->primary_index = unifycr_indexes[1];
    bgrm = mdhimGet(md, md->primary_index,
                    &local_key, sizeof(fattr_key_t), MDHIM_GET_EQ);
    if (!bgrm || bgrm->error) {
        rc = (int)UNIFYCR_ERROR_MDHIM;
    } else {
        fattr_val_t* ptr_val = (fattr_val_t*) bgrm->values[0];

        /* LOGDBG("rank:%d, getting fattr key:%d",
         *        glb_rank, ptr_fattr->gfid); */
        ptr_fattr->file_attr = ptr_val->file_attr;
        strncpy(ptr_fattr->filename, ptr_val->fname,
                sizeof(ptr_fattr->filename));

        rc = UNIFYCR_SUCCESS;
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

    app_config_t* app_config = (app_config_t*)arraylist_get(app_config_list,
                               app_id);

    size_t num_entries =
        *((size_t*)(app_config->shm_superblocks[client_side_id]
                    + app_config->meta_offset));

    /* indices are stored in the superblock shared memory
     *  created by the client*/
    int page_sz = getpagesize();
    unifycr_index_t* meta_payload =
        (unifycr_index_t*)(app_config->shm_superblocks[client_side_id]
                           + app_config->meta_offset + page_sz);

    md->primary_index = unifycr_indexes[0];

    size_t i;
    size_t used_entries = 0;
    for (i = 0; i < num_entries; i++) {
        if (gfid != meta_payload[i].fid) {
            continue;
        }

        unifycr_keys[used_entries]->fid = meta_payload[i].fid;
        unifycr_keys[used_entries]->offset = meta_payload[i].file_pos;

        unifycr_vals[used_entries]->addr = meta_payload[i].mem_pos;
        unifycr_vals[used_entries]->len = meta_payload[i].length;
        unifycr_vals[used_entries]->delegator_id = glb_rank;
        unifycr_vals[used_entries]->app_id = app_id;
        unifycr_vals[used_entries]->rank = client_side_id;

        // debug_log_key_val("before put",
        //                   unifycr_keys[used_entries],
        //                   unifycr_vals[used_entries]);

        unifycr_key_lens[used_entries] = sizeof(unifycr_key_t);
        unifycr_val_lens[used_entries] = sizeof(unifycr_val_t);
        used_entries++;
    }

    // print_fsync_indices(unifycr_keys, unifycr_vals, used_entries);

    brm = mdhimBPut(md, (void**)(&unifycr_keys[0]), unifycr_key_lens,
                    (void**)(&unifycr_vals[0]), unifycr_val_lens, used_entries,
                    NULL, NULL);
    brmp = brm;
    while (brmp) {
        if (brmp->error < 0) {
            LOGDBG("mdhimBPut returned error %d", brmp->error);
            ret = (int)UNIFYCR_ERROR_MDHIM;
        }
        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);
    }

    md->primary_index = unifycr_indexes[1];

    num_entries =
        *((size_t*)(app_config->shm_superblocks[client_side_id]
                    + app_config->fmeta_offset));

    /* file attributes are stored in the superblock shared memory
     * created by the client*/
    unifycr_file_attr_t* attr_payload =
        (unifycr_file_attr_t*)(app_config->shm_superblocks[client_side_id]
                               + app_config->fmeta_offset + page_sz);

    used_entries = 0;
    for (i = 0; i < num_entries; i++) {
        if (gfid != attr_payload[i].gfid) {
            continue;
        }

        *fattr_keys[used_entries] = attr_payload[i].gfid;
        fattr_vals[used_entries]->file_attr = attr_payload[i].file_attr;
        strcpy(fattr_vals[used_entries]->fname, attr_payload[i].filename);

        fattr_key_lens[used_entries] = sizeof(fattr_key_t);
        fattr_val_lens[used_entries] = sizeof(fattr_val_t);
        used_entries++;
    }

    brm = mdhimBPut(md, (void**)(&fattr_keys[0]), fattr_key_lens,
                    (void**)(&fattr_vals[0]), fattr_val_lens, used_entries,
                    NULL, NULL);
    brmp = brm;
    while (brmp) {
        if (brmp->error < 0) {
            LOGDBG("mdhimBPut returned error %d", brmp->error);
            ret = (int)UNIFYCR_ERROR_MDHIM;
        }
        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);
    }

    if (ret == (int)UNIFYCR_ERROR_MDHIM) {
        LOGDBG("Rank - %d: Error inserting keys/values into MDHIM",
               md->mdhim_rank);
    }

    return ret;
}

/* get the locations of all the requested file segment from
 * the key-value store.
 * @param app_id: client's application id
 * @param client_id: client-side process id
 * @param thrd_id: the thread created for processing
 *  its client's read requests.
 * @param dbg_rank: the client process's rank in its
 *  own application, used for debug purpose
 * @param gfid: global file id
 * @param offset: start offset for segment
 * @param length: segment length
 * @del_req_set: contains metadata information for all
 *  the read requests, such as the locations of the
 *  requested segments
 * @return success/error code
 */
int meta_read_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                  int gfid, size_t offset, size_t length,
                  msg_meta_t* del_req_set)
{
    /* assume we'll succeed, set to error otherwise */
    int rc = (int)UNIFYCR_SUCCESS;

    LOGDBG("app_id:%d, client_id:%d, thrd_id:%d, "
           "fid:%d, offset:%zu, length:%zu",
           app_id, client_id, thrd_id, gfid, offset, length);

    /* create key to describe first byte we'll read */
    unifycr_keys[0]->fid = gfid;
    unifycr_keys[0]->offset = offset;

    /* create key to describe last byte we'll read */
    unifycr_keys[1]->fid = gfid;
    unifycr_keys[1]->offset = offset + length - 1;

    unifycr_key_lens[0] = sizeof(unifycr_key_t);
    unifycr_key_lens[1] = sizeof(unifycr_key_t);

    /* get list of values for these keys */
    md->primary_index = unifycr_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void**)unifycr_keys,
                     unifycr_key_lens, 2, MDHIM_RANGE_BGET);

    /* track number of items we got */
    int tot_num = 0;

    /* iterate over each item returned by get */
    while (bgrm) {
        bgrmp = bgrm;
        /* check that we didn't hit an error with this item */
        if (bgrmp->error < 0) {
            rc = (int)UNIFYCR_ERROR_MDHIM;
        }

        /* iterate over each key and value in this item */
        unifycr_key_t* key;
        unifycr_val_t* val;
        size_t i;
        for (i = 0; i < bgrmp->num_keys; i++) {
            /* get pointer to key and value */
            key = (unifycr_key_t*) bgrmp->keys[i];
            val = (unifycr_val_t*) bgrmp->values[i];

            /* get pointer to next send_msg structure */
            send_msg_t* msg = &(del_req_set->msg_meta[tot_num]);
            memset(msg, 0, sizeof(send_msg_t));
            tot_num++;

            /* rank of the remote delegator */
            msg->dest_delegator_rank = val->delegator_id;

            /* dest_client_id and dest_app_id uniquely identify the remote
             * physical log file that contains the requested segments */
            msg->dest_app_id = val->app_id;
            msg->dest_client_id = val->rank;

            /* physical offset of the requested file segment on the log file */
            msg->dest_offset = (size_t) val->addr;
            msg->length = (size_t) val->len;

            /* src_app_id and src_cli_id identifies the requested client */
            msg->src_cli_id = client_id;
            msg->src_app_id = app_id;

            /* src_offset is the logical offset of the shared file */
            msg->src_offset = (size_t) key->offset;

            msg->src_dbg_rank = dbg_rank;
            msg->src_delegator_rank = glb_rank;
            msg->src_fid = (int) key->fid;
            msg->src_thrd = thrd_id;
        }

        /* advance to next item in list */
        bgrm = bgrmp->next;
        mdhim_full_release_msg(bgrmp);
    }

    /* record total number of entries */
    del_req_set->num = tot_num;

    return rc;
}

/* get the locations of all the requested file segments from
 * the key-value store.
 * @param app_id: client's application id
 * @param client_id: client-side process id
 * @param thrd_id: the thread created for processing
 *  its client's read requests.
 * @param dbg_rank: the client process's rank in its
 *  own application, used for debug purpose
 * @param reqbuf: memory buffer that contains
 *  the client's read requests
 * @param req_cnt: number of read requests
 * @del_req_set: contains metadata information for all
 *  the read requests, such as the locations of the
 *  requested segments
 * @return success/error code
 */
int meta_batch_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                   void* reqbuf, size_t req_cnt,
                   msg_meta_t* del_req_set)
{
    LOGDBG("app_id:%d, client_id:%d, thrd_id:%d",
           app_id, client_id, thrd_id);

    unifycr_ReadRequest_table_t readRequest = unifycr_ReadRequest_as_root(reqbuf);
    unifycr_Extent_vec_t extents = unifycr_ReadRequest_extents(readRequest);
    size_t extents_len = unifycr_Extent_vec_len(extents);
    assert(extents_len == req_cnt);

    int fid;
    size_t i, ndx, eoff, elen;
    for (i = 0; i < extents_len; i++) {
        ndx = 2 * i;
        fid = unifycr_Extent_fid(unifycr_Extent_vec_at(extents, i));
        eoff = unifycr_Extent_offset(unifycr_Extent_vec_at(extents, i));
        elen = unifycr_Extent_length(unifycr_Extent_vec_at(extents, i));
        LOGDBG("fid:%d, offset:%zu, length:%zu", fid, eoff, elen);

        unifycr_keys[ndx]->fid = fid;
        unifycr_keys[ndx]->offset = eoff;
        unifycr_keys[ndx + 1]->fid = fid;
        unifycr_keys[ndx + 1]->offset = eoff + elen - 1;
        unifycr_key_lens[ndx] = sizeof(unifycr_key_t);
        unifycr_key_lens[ndx + 1] = sizeof(unifycr_key_t);
    }

    md->primary_index = unifycr_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void**)unifycr_keys,
                     unifycr_key_lens, 2 * extents_len, MDHIM_RANGE_BGET);

    int rc = 0;
    int tot_num = 0;
    unifycr_key_t* key;
    unifycr_val_t* val;
    send_msg_t* msg;

    while (bgrm) {
        bgrmp = bgrm;
        if (bgrmp->error < 0) {
            rc = (int)UNIFYCR_ERROR_MDHIM;
        }

        for (i = 0; i < bgrmp->num_keys; i++) {
            key = (unifycr_key_t*) bgrmp->keys[i];
            val = (unifycr_val_t*) bgrmp->values[i];
            // debug_log_key_val("after get", key, val);

            msg = &(del_req_set->msg_meta[tot_num]);
            memset(msg, 0, sizeof(send_msg_t));
            tot_num++;

            /* rank of the remote delegator */
            msg->dest_delegator_rank = val->delegator_id;

            /* dest_client_id and dest_app_id uniquely identify the remote
             * physical log file that contains the requested segments */
            msg->dest_client_id = val->rank;
            msg->dest_app_id = val->app_id;

            /* physical offset of the requested file segment on the log file */
            msg->dest_offset = (size_t) val->addr;
            msg->length = (size_t) val->len;

            /* src_app_id and src_cli_id identifies the requested client */
            msg->src_app_id = app_id;
            msg->src_cli_id = client_id;

            /* src_offset is the logical offset of the shared file */
            msg->src_offset = (size_t) key->offset;

            msg->src_dbg_rank = dbg_rank;
            msg->src_delegator_rank = glb_rank;
            msg->src_fid = (int) key->fid;
            msg->src_thrd = thrd_id;
        }
        bgrm = bgrmp->next;
        mdhim_full_release_msg(bgrmp);
    }

    del_req_set->num = tot_num;
//    print_bget_indices(app_id, client_id, del_req_set->msg_meta, tot_num);

    return rc;
}

void print_bget_indices(int app_id, int cli_id,
                        send_msg_t* msgs, int tot_num)
{
    int i;
    for (i = 0; i < tot_num;  i++) {
        LOGDBG("index:dbg_rank:%d, dest_offset:%zu, "
               "dest_del_rank:%d, dest_cli_id:%d, dest_app_id:%d, "
               "length:%zu, src_app_id:%d, src_cli_id:%d, src_offset:%zu, "
               "src_del_rank:%d, src_fid:%d, num:%d",
               msgs[i].src_dbg_rank, msgs[i].dest_offset,
               msgs[i].dest_delegator_rank, msgs[i].dest_client_id,
               msgs[i].dest_app_id, msgs[i].length,
               msgs[i].src_app_id, msgs[i].src_cli_id,
               msgs[i].src_offset, msgs[i].src_delegator_rank,
               msgs[i].src_fid, tot_num);
    }
}

void print_fsync_indices(unifycr_key_t** unifycr_keys,
                         unifycr_val_t** unifycr_vals,
                         size_t num_entries)
{
    size_t i;
    for (i = 0; i < num_entries; i++) {
        LOGDBG("fid:%lu, offset:%lu, addr:%lu, len:%lu, del_id:%d",
               unifycr_keys[i]->fid, unifycr_keys[i]->offset,
               unifycr_vals[i]->addr, unifycr_vals[i]->len,
               unifycr_vals[i]->delegator_id);
    }
}

int meta_free_indices()
{
    int i;
    for (i = 0; i < MAX_META_PER_SEND; i++) {
        free(unifycr_keys[i]);
    }
    free(unifycr_keys);

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        free(unifycr_vals[i]);
    }
    free(unifycr_vals);

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
