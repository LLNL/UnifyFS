/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
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

#define _XOPEN_SOURCE 500
#include <assert.h>
#include <ftw.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "arraylist.h"
#include "indexes.h"
#include "mdhim.h"
#include "unifycr_log.h"
#include "unifycr_metadata.h"
#include "unifycr_client_rpcs.h"
#include "ucr_read_builder.h"

unifycr_key_t** unifycr_keys;
unifycr_val_t** unifycr_vals;

fattr_key_t** fattr_keys;
fattr_val_t** fattr_vals;

char* manifest_path;

struct mdhim_brm_t* brm, *brmp;
struct mdhim_bgetrm_t* bgrm, *bgrmp;

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
        LOGDBG("@%s - key(fid=%d, offset=%lu), "
               "val(del=%d, len=%lu, addr=%lu, app=%d, rank=%d)",
               ctx, key->fid, key->offset,
               val->delegator_rank, val->len, val->addr,
               val->app_id, val->rank);
    } else if (key != NULL) {
        LOGDBG("@%s - key(fid=%d, offset=%lu)",
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
    int rc, ratio;
    size_t path_len;
    long svr_ratio, range_sz;
    MPI_Comm comm = MPI_COMM_WORLD;

    if (cfg == NULL) {
        return -1;
    }

    mdhim_options_t* db_opts = mdhim_options_init();
    if (db_opts == NULL) {
        return -1;
    }
    mdhim_options_set_db_type(db_opts, LEVELDB);
    mdhim_options_set_db_name(db_opts, cfg->meta_db_name);
    mdhim_options_set_key_type(db_opts, MDHIM_UNIFYCR_KEY);
    mdhim_options_set_debug_level(db_opts, MLOG_CRIT);

    /* UNIFYCR_META_DB_PATH: root directory for metadata */
    mdhim_options_set_db_path(db_opts, cfg->meta_db_path);

    /* number of metadata servers =
     *   number of unifycr servers / UNIFYCR_META_SERVER_RATIO */
    svr_ratio = 0;
    rc = configurator_int_val(cfg->meta_server_ratio, &svr_ratio);
    if (rc != 0) {
        return -1;
    }
    ratio = (int) svr_ratio;
    mdhim_options_set_server_factor(db_opts, ratio);

    /* indices/attributes are striped to servers according
     * to config setting for UNIFYCR_META_RANGE_SIZE. */
    range_sz = 0;
    rc = configurator_int_val(cfg->meta_range_size, &range_sz);
    if (rc != 0) {
        return -1;
    }
    max_recs_per_slice = (size_t) range_sz;
    mdhim_options_set_max_recs_per_slice(db_opts, (uint64_t)range_sz);

    md = mdhimInit(&comm, db_opts);

    /*this index is created for storing index metadata*/
    unifycr_indexes[0] = md->primary_index;

    /*this index is created for storing file attribute metadata*/
    unifycr_indexes[1] = create_global_index(md, ratio, 1,
                                             LEVELDB, MDHIM_INT_KEY,
                                             "file_attr");

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
int meta_init_indices(void)
{
    int i;

    /* init index metadata */
    unifycr_keys = (unifycr_key_t**)
        calloc(MAX_META_PER_SEND, sizeof(unifycr_key_t*));
    if (unifycr_keys == NULL) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    unifycr_vals = (unifycr_val_t**)
        calloc(MAX_META_PER_SEND, sizeof(unifycr_val_t*));
    if (unifycr_vals == NULL) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifycr_keys[i] = (unifycr_key_t*) calloc(1, sizeof(unifycr_key_t));
        if (unifycr_keys[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }

        unifycr_vals[i] = (unifycr_val_t*) calloc(1, sizeof(unifycr_val_t));
        if (unifycr_vals[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
    }

    /* init attribute metadata */
    fattr_keys = (fattr_key_t**)
        calloc(MAX_FILE_CNT_PER_NODE, sizeof(fattr_key_t*));
    if (fattr_keys == NULL) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    fattr_vals = (fattr_val_t**)
        calloc(MAX_FILE_CNT_PER_NODE, sizeof(fattr_val_t*));
    if (fattr_vals == NULL) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_keys[i] = (fattr_key_t*) calloc(1, sizeof(fattr_key_t));
        if (fattr_keys[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }

        fattr_vals[i] = (fattr_val_t*) calloc(1, sizeof(fattr_val_t));
        if (fattr_vals[i] == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }
    }

    return 0;
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

void print_fsync_indices(unifycr_key_t** keys,
                         unifycr_val_t** vals,
                         size_t num_entries)
{
    size_t i;
    for (i = 0; i < num_entries; i++) {
        LOGDBG("fid:%d, offset:%lu, addr:%lu, len:%lu, del_id:%d",
               keys[i]->fid, keys[i]->offset,
               vals[i]->addr, vals[i]->len,
               vals[i]->delegator_rank);
    }
}

void meta_free_indices(void)
{
    int i;
    if (NULL != unifycr_keys) {
        for (i = 0; i < MAX_META_PER_SEND; i++) {
            if (NULL != unifycr_keys[i]) {
                free(unifycr_keys[i]);
            }
            if (NULL != unifycr_vals[i]) {
                free(unifycr_vals[i]);
            }
        }
        free(unifycr_keys);
        free(unifycr_vals);
    }
    if (NULL != fattr_keys) {
        for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
            if (NULL != fattr_keys[i]) {
                free(fattr_keys[i]);
            }
            if (NULL != fattr_vals[i]) {
                free(fattr_vals[i]);
            }
        }
        free(fattr_keys);
        free(fattr_vals);
    }
}

static int remove_cb(const char* fpath, const struct stat* sb,
                     int typeflag, struct FTW* ftwbuf)
{
    int rc = remove(fpath);
    if (rc) {
        LOGERR("failed to remove(%s)", fpath);
    }
    return rc;
}

static int remove_mdhim_db_filetree(char* db_root_path)
{
    LOGDBG("remove MDHIM DB filetree at %s", db_root_path);
    return nftw(db_root_path, remove_cb, 64, FTW_DEPTH | FTW_PHYS);
}


int meta_sanitize(void)
{
    int rc;
    char dbpath[UNIFYCR_MAX_FILENAME] = {0};

    snprintf(dbpath, sizeof(dbpath), "%s", md->db_opts->db_path);

    mdhimClose(md);
    md = NULL;

    rc = remove_mdhim_db_filetree(dbpath);
    if (rc) {
        LOGERR("failure during MDHIM file tree removal");
    }

    meta_free_indices();

    return UNIFYCR_SUCCESS;
}

// New API
/*
 *
 */
int unifycr_set_file_attribute(unifycr_file_attr_t* fattr_ptr)
{
    int rc = UNIFYCR_SUCCESS;

    int gfid = fattr_ptr->gfid;

    md->primary_index = unifycr_indexes[1];
    brm = mdhimPut(md, &gfid, sizeof(int),
                   fattr_ptr, sizeof(unifycr_file_attr_t),
                   NULL, NULL);
    if (!brm || brm->error) {
        // return UNIFYCR_ERROR_MDHIM on error
        rc = (int)UNIFYCR_ERROR_MDHIM;
    }

    mdhim_full_release_msg(brm);

    return rc;
}

/*
 *
 */
int unifycr_set_file_attributes(int num_entries,
                                fattr_key_t** keys, int* key_lens,
                                unifycr_file_attr_t** fattr_ptr, int* val_lens)
{
    int rc = UNIFYCR_SUCCESS;

    md->primary_index = unifycr_indexes[1];
    brm = mdhimBPut(md, (void**)keys, key_lens, (void**)fattr_ptr,
                    val_lens, num_entries, NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        rc = (int)UNIFYCR_ERROR_MDHIM;
        LOGERR("Error inserting keys/values into MDHIM");
    }

    while (brmp) {
        if (brmp->error < 0) {
            rc = (int)UNIFYCR_ERROR_MDHIM;
            break;
        }

        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);
    }

    return rc;
}

/*
 *
 */
int unifycr_get_file_attribute(int gfid,
                               unifycr_file_attr_t* attr_val_ptr)
{
    int rc = UNIFYCR_SUCCESS;
    unifycr_file_attr_t* tmp_ptr_attr;

    md->primary_index = unifycr_indexes[1];
    bgrm = mdhimGet(md, md->primary_index, &gfid,
                    sizeof(int), MDHIM_GET_EQ);
    if (!bgrm || bgrm->error) {
        rc = (int)UNIFYCR_ERROR_MDHIM;
    } else {
        tmp_ptr_attr = (unifycr_file_attr_t*)bgrm->values[0];
        memcpy(attr_val_ptr, tmp_ptr_attr, sizeof(unifycr_file_attr_t));
        mdhim_full_release_msg(bgrm);
    }

    return rc;
}

/*
 *
 */
int unifycr_get_file_extents(int num_keys, unifycr_key_t** keys,
                             int* unifycr_key_lens, int* num_values,
                             unifycr_keyval_t** keyvals)
{
    /*
     * This is using a modified version of mdhim. The function will return all
     * key-value pairs within the range of the key tuple.
     * We need to re-evaluate this function to use different key-value stores.
     */

    int i;
    int rc = UNIFYCR_SUCCESS;
    int tot_num = 0;

    unifycr_key_t* tmp_key;
    unifycr_val_t* tmp_val;
    unifycr_keyval_t* kviter = *keyvals;

    md->primary_index = unifycr_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void**)keys,
                     unifycr_key_lens, num_keys, MDHIM_RANGE_BGET);

    while (bgrm) {
        bgrmp = bgrm;
        if (bgrmp->error < 0) {
            // TODO: need better error handling
            rc = (int)UNIFYCR_ERROR_MDHIM;
            return rc;
        }

        if (tot_num < MAX_META_PER_SEND) {
            for (i = 0; i < bgrmp->num_keys; i++) {
                tmp_key = (unifycr_key_t*)bgrmp->keys[i];
                tmp_val = (unifycr_val_t*)bgrmp->values[i];
                memcpy(&(kviter->key), tmp_key, sizeof(unifycr_key_t));
                memcpy(&(kviter->val), tmp_val, sizeof(unifycr_val_t));
                kviter++;
                tot_num++;
                if (MAX_META_PER_SEND == tot_num) {
                    LOGERR("Error: maximum number of values!");
                    rc = UNIFYCR_FAILURE;
                    break;
                }
            }
        }
        bgrm = bgrmp->next;
        mdhim_full_release_msg(bgrmp);
    }

    *num_values = tot_num;

    return rc;
}

/*
 *
 */
int unifycr_set_file_extents(int num_entries,
                             unifycr_key_t** keys, int* unifycr_key_lens,
                             unifycr_val_t** vals, int* unifycr_val_lens)
{
    int rc = UNIFYCR_SUCCESS;

    md->primary_index = unifycr_indexes[0];

    brm = mdhimBPut(md, (void**)(keys), unifycr_key_lens,
                    (void**)(vals), unifycr_val_lens, num_entries,
                    NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        rc = (int)UNIFYCR_ERROR_MDHIM;
        LOGERR("Error inserting keys/values into MDHIM");
    }

    while (brmp) {
        if (brmp->error < 0) {
            rc = (int)UNIFYCR_ERROR_MDHIM;
            break;
        }

        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);
    }

    return rc;
}

