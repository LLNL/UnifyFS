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
#include "unifyfs_metadata_mdhim.h"

// MDHIM headers
#include "indexes.h"
#include "mdhim.h"

/* maps a global file id to its extent map */
struct unifyfs_inode_tree meta_inode_tree;

struct mdhim_t* md;

/* we use two MDHIM indexes:
 *   0) for file extents
 *   1) for file attributes */
#define IDX_FILE_EXTENTS (0)
#define IDX_FILE_ATTR    (1)
struct index_t* unifyfs_indexes[2];

size_t meta_slice_sz;

void debug_log_key_val(const char* ctx,
                       unifyfs_key_t* key,
                       unifyfs_val_t* val)
{
    if ((key != NULL) && (val != NULL)) {
        LOGDBG("@%s - key(gfid=%d, offset=%lu), "
               "val(del=%d, len=%lu, addr=%lu, app=%d, rank=%d)",
               ctx, key->gfid, key->offset,
               val->delegator_rank, val->len, val->addr,
               val->app_id, val->rank);
    } else if (key != NULL) {
        LOGDBG("@%s - key(gfid=%d, offset=%lu)",
               ctx, key->gfid, key->offset);
    }
}

int unifyfs_key_compare(unifyfs_key_t* a, unifyfs_key_t* b)
{
    assert((NULL != a) && (NULL != b));
    if (a->gfid == b->gfid) {
        if (a->offset == b->offset) {
            return 0;
        } else if (a->offset < b->offset) {
            return -1;
        } else {
            return 1;
        }
    } else if (a->gfid < b->gfid) {
        return -1;
    } else {
        return 1;
    }
}

/* initialize the key-value store */
int meta_init_store(unifyfs_cfg_t* cfg)
{
    int rc, ratio;
    MPI_Comm comm = MPI_COMM_WORLD;
    size_t path_len;
    long svr_ratio, range_sz;
    struct stat ss;
    char db_path[UNIFYFS_MAX_FILENAME] = {0};

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
    snprintf(db_path, sizeof(db_path), "%s/mdhim", cfg->meta_db_path);
    rc = stat(db_path, &ss);
    if (rc != 0) {
        rc = mkdir(db_path, 0770);
        if (rc != 0) {
            LOGERR("failed to create MDHIM metadata directory %s", db_path);
            return -1;
        }
    }
    mdhim_options_set_db_path(db_opts, strdup(db_path));

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
    meta_slice_sz = (size_t) range_sz;
    mdhim_options_set_max_recs_per_slice(db_opts, (uint64_t)range_sz);

    md = mdhimInit(&comm, db_opts);

    /* index for storing file extent metadata */
    unifyfs_indexes[IDX_FILE_EXTENTS] = md->primary_index;

    /* index for storing file attribute metadata */
    unifyfs_indexes[IDX_FILE_ATTR] = create_global_index(md,
        ratio, 1, LEVELDB, MDHIM_INT_KEY, "file_attr");

    /* initialize our tree that maps a gfid to its extent tree */
    unifyfs_inode_tree_init(&meta_inode_tree);

    return 0;
}

void print_fsync_indices(unifyfs_key_t** keys,
                         unifyfs_val_t** vals,
                         size_t num_entries)
{
    size_t i;
    for (i = 0; i < num_entries; i++) {
        LOGDBG("gfid:%d, offset:%lu, addr:%lu, len:%lu, del_id:%d",
               keys[i]->gfid, keys[i]->offset,
               vals[i]->addr, vals[i]->len,
               vals[i]->delegator_rank);
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
    char db_path[UNIFYFS_MAX_FILENAME] = {0};

    // capture db_path before closing MDHIM
    snprintf(db_path, sizeof(db_path), "%s", md->db_opts->db_path);

    mdhimClose(md);
    md = NULL;

    // remove the metadata filetree
    rc = remove_mdhim_db_filetree(db_path);
    if (rc) {
        LOGERR("failure during MDHIM file tree removal");
    }

    /* tear down gfid-to-extents tree */
    unifyfs_inode_tree_destroy(&meta_inode_tree);

    return UNIFYFS_SUCCESS;
}

// New API
/*
 *
 */
int unifyfs_set_file_attribute(
    int set_size,
    int set_laminate,
    unifyfs_file_attr_t* fattr_ptr)
{
    int rc = UNIFYFS_SUCCESS;

    /* select index for file attributes */
    md->primary_index = unifyfs_indexes[IDX_FILE_ATTR];

    int gfid = fattr_ptr->gfid;

    /* if we want to preserve some settings,
     * we copy those fields from attributes
     * on the existing entry, if there is one */
    int preserve = (!set_size || !set_laminate);
    if (preserve) {
        /* lookup existing attributes for the file */
        unifyfs_file_attr_t attr;
        int get_rc = unifyfs_get_file_attribute(gfid, &attr);
        if (get_rc == UNIFYFS_SUCCESS) {
            /* found the attributes for this file,
             * if size flag is not set, preserve existing size value */
            if (!set_size) {
                fattr_ptr->size = attr.size;
            }

            /* if laminate flag is not set,
             * preserve existing is_laminated state */
            if (!set_laminate) {
                fattr_ptr->is_laminated = attr.is_laminated;
            }
        } else {
            /* otherwise, trying to update attributes for a file that
             * we can't find */
            return get_rc;
        }
    }

    /* insert file attribute for given global file id */
    struct mdhim_brm_t* brm = mdhimPut(md,
        &gfid, sizeof(int),
        fattr_ptr, sizeof(unifyfs_file_attr_t),
        NULL, NULL);

    if (!brm || brm->error) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    }

    if (brm) {
        mdhim_full_release_msg(brm);
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to insert attributes for gfid=%d", gfid);
    }
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

    /* select index for file attributes */
    md->primary_index = unifyfs_indexes[IDX_FILE_ATTR];

    /* put list of key/value pairs */
    struct mdhim_brm_t* brm = mdhimBPut(md,
        (void**)keys, key_lens,
        (void**)fattr_ptr, val_lens,
        num_entries, NULL, NULL);

    /* check for errors and free resources */
    if (!brm) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        /* step through linked list of messages,
         * scan for any error and free messages */
        struct mdhim_brm_t* brmp = brm;
        while (brmp) {
            /* check current item for error */
            if (brmp->error) {
                LOGERR("MDHIM bulk put error=%d", brmp->error);
                rc = (int)UNIFYFS_ERROR_MDHIM;
            }

            /* record pointer to current item,
             * advance loop pointer to next item in list,
             * free resources for current item */
            brm  = brmp;
            brmp = brmp->next;
            mdhim_full_release_msg(brm);
        }
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to bulk insert file attributes");
    }
    return rc;
}

/* given a global file id, lookup and return file attributes */
int unifyfs_get_file_attribute(
    int gfid,
    unifyfs_file_attr_t* attr)
{
    int rc = UNIFYFS_SUCCESS;

    /* select index holding file attributes,
     * execute lookup for given file id */
    md->primary_index = unifyfs_indexes[IDX_FILE_ATTR];
    struct mdhim_bgetrm_t* bgrm = mdhimGet(md, md->primary_index,
        &gfid, sizeof(int), MDHIM_GET_EQ);

    if (!bgrm || bgrm->error) {
        /* failed to find info for this file id */
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        /* copy file attribute from value into output parameter */
        unifyfs_file_attr_t* ptr = (unifyfs_file_attr_t*)bgrm->values[0];
        memcpy(attr, ptr, sizeof(unifyfs_file_attr_t));
    }

    /* free resources returned from lookup */
    if (bgrm) {
        mdhim_full_release_msg(bgrm);
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to retrieve attributes for gfid=%d", gfid);
    }
    return rc;
}

/* given a global file id, delete file attributes */
int unifyfs_delete_file_attribute(
    int gfid)
{
    int rc = UNIFYFS_SUCCESS;

    /* select index holding file attributes,
     * delete entry for given file id */
    md->primary_index = unifyfs_indexes[IDX_FILE_ATTR];
    struct mdhim_brm_t* brm = mdhimDelete(md, md->primary_index,
        &gfid, sizeof(int));

    /* check for errors and free resources */
    if (!brm) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        /* step through linked list of messages,
         * scan for any error and free messages */
        struct mdhim_brm_t* brmp = brm;
        while (brmp) {
            /* check current item for error */
            if (brmp->error) {
                LOGERR("MDHIM delete error=%d", brmp->error);
                rc = (int)UNIFYFS_ERROR_MDHIM;
            }

            /* record pointer to current item,
             * advance loop pointer to next item in list,
             * free resources for current item */
            brm  = brmp;
            brmp = brmp->next;
            mdhim_full_release_msg(brm);
        }
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to delete attributes for gfid=%d", gfid);
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
    int rc = UNIFYFS_SUCCESS;

    /* initialize output values */
    *num_values = 0;
    *keyvals = NULL;

    /* select index for file extents */
    md->primary_index = unifyfs_indexes[IDX_FILE_EXTENTS];

    /* execute range query */
    struct mdhim_bgetrm_t* bkvlist = mdhimBGet(md, md->primary_index,
        (void**)keys, key_lens, num_keys, MDHIM_RANGE_BGET);

    /* iterate over each item in list, check for errors
     * and sum up total number of key/value pairs we got back */
    size_t tot_num = 0;
    struct mdhim_bgetrm_t* ptr = bkvlist;
    while (ptr) {
        /* check that we don't have an error condition */
        if (ptr->error) {
            /* hit an error */
            LOGERR("MDHIM range query error=%d", ptr->error);
            return (int)UNIFYFS_ERROR_MDHIM;
        }

        /* total up number of key/values returned */
        tot_num += (size_t) ptr->num_keys;

        /* get pointer to next item in the list */
        ptr = ptr->next;
    }

    /* allocate memory to copy key/value data */
    unifyfs_keyval_t* kvs = (unifyfs_keyval_t*) calloc(
        tot_num, sizeof(unifyfs_keyval_t));
    if (NULL == kvs) {
        LOGERR("failed to allocate keyvals");
        return ENOMEM;
    }

    /* iterate over list and copy each key/value into output array */
    ptr = bkvlist;
    unifyfs_keyval_t* kviter = kvs;
    while (ptr) {
        /* iterate over key/value in list element */
        int i;
        for (i = 0; i < ptr->num_keys; i++) {
            /* get pointer to current key and value */
            unifyfs_key_t* tmp_key = (unifyfs_key_t*)ptr->keys[i];
            unifyfs_val_t* tmp_val = (unifyfs_val_t*)ptr->values[i];

            /* copy contents over to output array */
            memcpy(&(kviter->key), tmp_key, sizeof(unifyfs_key_t));
            memcpy(&(kviter->val), tmp_val, sizeof(unifyfs_val_t));

            /* bump up to next element in output array */
            kviter++;
        }

        /* get pointer to next item in the list */
        struct mdhim_bgetrm_t* next = ptr->next;

        /* release resources for the curren item */
        mdhim_full_release_msg(ptr);
        ptr = next;
    }

    /* set output values */
    *num_values = tot_num;
    *keyvals = kvs;

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

    /* select index for file extents */
    md->primary_index = unifyfs_indexes[IDX_FILE_EXTENTS];

    /* put list of key/value pairs */
    struct mdhim_brm_t* brm = mdhimBPut(md,
        (void**)(keys), key_lens,
        (void**)(vals), val_lens,
        num_entries, NULL, NULL);

    /* check for errors and free resources */
    if (!brm) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        /* step through linked list of messages,
         * scan for any error and free messages */
        struct mdhim_brm_t* brmp = brm;
        while (brmp) {
            /* check current item for error */
            if (brmp->error) {
                LOGERR("MDHIM bulk put error=%d", brmp->error);
                rc = (int)UNIFYFS_ERROR_MDHIM;
            }

            /* record pointer to current item,
             * advance loop pointer to next item in list,
             * free resources for current item */
            brm  = brmp;
            brmp = brmp->next;
            mdhim_full_release_msg(brm);
        }
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to bulk insert file extents");
    }
    return rc;
}

/* delete the listed keys from the file extents */
int unifyfs_delete_file_extents(
    int num_entries,      /* number of items in keys list */
    unifyfs_key_t** keys, /* list of keys to be deleted */
    int* key_lens)        /* list of byte sizes for keys list items */
{
    /* assume we'll succeed */
    int rc = UNIFYFS_SUCCESS;

    /* select index for file extents */
    md->primary_index = unifyfs_indexes[IDX_FILE_EXTENTS];

    /* delete list of key/value pairs */
    struct mdhim_brm_t* brm = mdhimBDelete(md, md->primary_index,
        (void**)(keys), key_lens, num_entries);

    /* check for errors and free resources */
    if (!brm) {
        rc = (int)UNIFYFS_ERROR_MDHIM;
    } else {
        /* step through linked list of messages,
         * scan for any error and free messages */
        struct mdhim_brm_t* brmp = brm;
        while (brmp) {
            /* check current item for error */
            if (brmp->error) {
                LOGERR("MDHIM bulk delete error=%d", brmp->error);
                rc = (int)UNIFYFS_ERROR_MDHIM;
            }

            /* record pointer to current item,
             * advance loop pointer to next item in list,
             * free resources for current item */
            brm  = brmp;
            brmp = brmp->next;
            mdhim_full_release_msg(brm);
        }
    }

    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to bulk delete file extents");
    }
    return rc;
}
