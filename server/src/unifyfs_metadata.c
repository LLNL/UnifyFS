/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
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
 * Written by: Teng Wang, Adam Moody, Wekuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICNSE for full license text.
 */

// NOTE: following two lines needed for nftw(), MUST COME FIRST IN FILE
#define _XOPEN_SOURCE 500
#include <ftw.h>

// common headers
#include "unifyfs_client_rpcs.h"
#include "ucr_read_builder.h"

// server headers
#include "unifyfs_global.h"
#include "unifyfs_metadata.h"

// MDHIM headers
#include "indexes.h"
#include "mdhim.h"


unifyfs_key_t** unifyfs_keys;
unifyfs_val_t** unifyfs_vals;

fattr_key_t** fattr_keys;
fattr_val_t** fattr_vals;

char* manifest_path;

struct mdhim_brm_t* brm, *brmp;
struct mdhim_bgetrm_t* bgrm, *bgrmp;

struct mdhim_t* md;
int md_size;

struct index_t* unifyfs_indexes[2];
size_t max_recs_per_slice;

void debug_log_key_val(const char* ctx,
                       unifyfs_key_t* key,
                       unifyfs_val_t* val)
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

int unifyfs_key_compare(unifyfs_key_t* a, unifyfs_key_t* b)
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

/* initialize the key-value store */
int meta_init_store(unifyfs_cfg_t* cfg)
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
    mdhim_options_set_key_type(db_opts, MDHIM_UNIFYFS_KEY);
    mdhim_options_set_debug_level(db_opts, MLOG_CRIT);

    /* UNIFYFS_META_DB_PATH: root directory for metadata */
    mdhim_options_set_db_path(db_opts, cfg->meta_db_path);

    /* number of metadata servers =
     *   number of unifyfs servers / UNIFYFS_META_SERVER_RATIO */
    svr_ratio = 0;
    rc = configurator_int_val(cfg->meta_server_ratio, &svr_ratio);
    if (rc != 0) {
        return -1;
    }
    ratio = (int) svr_ratio;
    mdhim_options_set_server_factor(db_opts, ratio);

    /* indices/attributes are striped to servers according
     * to config setting for UNIFYFS_META_RANGE_SIZE. */
    range_sz = 0;
    rc = configurator_int_val(cfg->meta_range_size, &range_sz);
    if (rc != 0) {
        return -1;
    }
    max_recs_per_slice = (size_t) range_sz;
    mdhim_options_set_max_recs_per_slice(db_opts, (uint64_t)range_sz);

    md = mdhimInit(&comm, db_opts);

    /*this index is created for storing index metadata*/
    unifyfs_indexes[0] = md->primary_index;

    /*this index is created for storing file attribute metadata*/
    unifyfs_indexes[1] = create_global_index(md, ratio, 1,
                                             LEVELDB, MDHIM_INT_KEY,
                                             "file_attr");

    MPI_Comm_size(md->mdhim_comm, &md_size);

    rc = meta_init_indices();
    if (rc != 0) {
        return -1;
    }

    return 0;
}

/* initialize the key and value list used to put/get key-value pairs
 * TODO: split once the number of metadata exceeds MAX_META_PER_SEND */
int meta_init_indices(void)
{
    int i;

    /* init index metadata */
    unifyfs_keys = (unifyfs_key_t**)
        calloc(MAX_META_PER_SEND, sizeof(unifyfs_key_t*));
    if (unifyfs_keys == NULL) {
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    unifyfs_vals = (unifyfs_val_t**)
        calloc(MAX_META_PER_SEND, sizeof(unifyfs_val_t*));
    if (unifyfs_vals == NULL) {
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    for (i = 0; i < MAX_META_PER_SEND; i++) {
        unifyfs_keys[i] = (unifyfs_key_t*) calloc(1, sizeof(unifyfs_key_t));
        if (unifyfs_keys[i] == NULL) {
            return (int)UNIFYFS_ERROR_NOMEM;
        }

        unifyfs_vals[i] = (unifyfs_val_t*) calloc(1, sizeof(unifyfs_val_t));
        if (unifyfs_vals[i] == NULL) {
            return (int)UNIFYFS_ERROR_NOMEM;
        }
    }

    /* init attribute metadata */
    fattr_keys = (fattr_key_t**)
        calloc(MAX_FILE_CNT_PER_NODE, sizeof(fattr_key_t*));
    if (fattr_keys == NULL) {
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    fattr_vals = (fattr_val_t**)
        calloc(MAX_FILE_CNT_PER_NODE, sizeof(fattr_val_t*));
    if (fattr_vals == NULL) {
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    for (i = 0; i < MAX_FILE_CNT_PER_NODE; i++) {
        fattr_keys[i] = (fattr_key_t*) calloc(1, sizeof(fattr_key_t));
        if (fattr_keys[i] == NULL) {
            return (int)UNIFYFS_ERROR_NOMEM;
        }

        fattr_vals[i] = (fattr_val_t*) calloc(1, sizeof(fattr_val_t));
        if (fattr_vals[i] == NULL) {
            return (int)UNIFYFS_ERROR_NOMEM;
        }
    }

    return 0;
}

void print_fsync_indices(unifyfs_key_t** keys,
                         unifyfs_val_t** vals,
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
    if (NULL != unifyfs_keys) {
        for (i = 0; i < MAX_META_PER_SEND; i++) {
            if (NULL != unifyfs_keys[i]) {
                free(unifyfs_keys[i]);
            }
            if (NULL != unifyfs_vals[i]) {
                free(unifyfs_vals[i]);
            }
        }
        free(unifyfs_keys);
        free(unifyfs_vals);
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
    char dbpath[UNIFYFS_MAX_FILENAME] = {0};

    snprintf(dbpath, sizeof(dbpath), "%s", md->db_opts->db_path);

    mdhimClose(md);
    md = NULL;

    rc = remove_mdhim_db_filetree(dbpath);
    if (rc) {
        LOGERR("failure during MDHIM file tree removal");
    }

    meta_free_indices();

    return UNIFYFS_SUCCESS;
}

// New API
/*
 *
 */
int unifyfs_set_file_attribute(unifyfs_file_attr_t* fattr_ptr)
{
    int rc = UNIFYFS_SUCCESS;

    int gfid = fattr_ptr->gfid;

    md->primary_index = unifyfs_indexes[1];
    brm = mdhimPut(md, &gfid, sizeof(int),
                   fattr_ptr, sizeof(unifyfs_file_attr_t),
                   NULL, NULL);
    if (!brm || brm->error) {
        // return UNIFYFS_ERROR_MDHIM on error
        rc = (int)UNIFYFS_ERROR_MDHIM;
    }

    mdhim_full_release_msg(brm);

    return rc;
}

/*
 *
 */
int unifyfs_set_file_attributes(int num_entries,
                                fattr_key_t** keys, int* key_lens,
                                unifyfs_file_attr_t** fattr_ptr, int* val_lens)
{
    int rc = UNIFYFS_SUCCESS;

    md->primary_index = unifyfs_indexes[1];
    brm = mdhimBPut(md, (void**)keys, key_lens, (void**)fattr_ptr,
                    val_lens, num_entries, NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
        LOGERR("Error inserting keys/values into MDHIM");
    }

    while (brmp) {
        if (brmp->error < 0) {
            rc = (int)UNIFYFS_ERROR_MDHIM;
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
int unifyfs_get_file_attribute(int gfid,
                               unifyfs_file_attr_t* attr_val_ptr)
{
    int rc = UNIFYFS_SUCCESS;
    unifyfs_file_attr_t* tmp_ptr_attr;

    md->primary_index = unifyfs_indexes[1];
    bgrm = mdhimGet(md, md->primary_index, &gfid,
                    sizeof(int), MDHIM_GET_EQ);
    if (!bgrm || bgrm->error) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        tmp_ptr_attr = (unifyfs_file_attr_t*)bgrm->values[0];
        memcpy(attr_val_ptr, tmp_ptr_attr, sizeof(unifyfs_file_attr_t));
        mdhim_full_release_msg(bgrm);
    }

    return rc;
}

/*
 *
 */
int unifyfs_get_file_extents(int num_keys, unifyfs_key_t** keys,
                             int* key_lens, int* num_values,
                             unifyfs_keyval_t** keyvals)
{
    /*
     * This is using a modified version of mdhim. The function will return all
     * key-value pairs within the range of the key tuple.
     * We need to re-evaluate this function to use different key-value stores.
     */

    int i;
    int rc = UNIFYFS_SUCCESS;
    int tot_num = 0;

    unifyfs_key_t* tmp_key;
    unifyfs_val_t* tmp_val;
    unifyfs_keyval_t* kviter = *keyvals;

    md->primary_index = unifyfs_indexes[0];
    bgrm = mdhimBGet(md, md->primary_index, (void**)keys,
                     key_lens, num_keys, MDHIM_RANGE_BGET);

    while (bgrm) {
        bgrmp = bgrm;
        if (bgrmp->error < 0) {
            // TODO: need better error handling
            rc = (int)UNIFYFS_ERROR_MDHIM;
            return rc;
        }

        if (tot_num < MAX_META_PER_SEND) {
            for (i = 0; i < bgrmp->num_keys; i++) {
                tmp_key = (unifyfs_key_t*)bgrmp->keys[i];
                tmp_val = (unifyfs_val_t*)bgrmp->values[i];
                memcpy(&(kviter->key), tmp_key, sizeof(unifyfs_key_t));
                memcpy(&(kviter->val), tmp_val, sizeof(unifyfs_val_t));
                kviter++;
                tot_num++;
                if (MAX_META_PER_SEND == tot_num) {
                    LOGERR("Error: maximum number of values!");
                    rc = UNIFYFS_FAILURE;
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
int unifyfs_set_file_extents(int num_entries,
                             unifyfs_key_t** keys, int* key_lens,
                             unifyfs_val_t** vals, int* val_lens)
{
    int rc = UNIFYFS_SUCCESS;

    md->primary_index = unifyfs_indexes[0];

    brm = mdhimBPut(md, (void**)(keys), key_lens,
                    (void**)(vals), val_lens, num_entries,
                    NULL, NULL);
    brmp = brm;
    if (!brmp || brmp->error) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
        LOGERR("Error inserting keys/values into MDHIM");
    }

    while (brmp) {
        if (brmp->error < 0) {
            rc = (int)UNIFYFS_ERROR_MDHIM;
            break;
        }

        brm = brmp;
        brmp = brmp->next;
        mdhim_full_release_msg(brm);
    }

    return rc;
}

