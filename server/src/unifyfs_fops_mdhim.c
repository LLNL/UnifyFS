/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "unifyfs_group_rpc.h"
#include "unifyfs_metadata_mdhim.h"
#include "unifyfs_request_manager.h"

/* given an extent corresponding to a write index, create new key/value
 * pairs for that extent, splitting into multiple keys at the slice
 * range boundaries (meta_slice_sz), it returns the number of
 * newly created key/values inserted into the given key and value
 * arrays */
static int split_index(
    unifyfs_key_t** keys, /* list to add newly created keys into */
    unifyfs_val_t** vals, /* list to add newly created values into */
    int* keylens,         /* list for size of each key */
    int* vallens,         /* list for size of each value */
    int gfid,             /* global file id of write */
    size_t offset,        /* starting byte offset of extent */
    size_t length,        /* number of bytes in extent */
    size_t log_offset,    /* offset within data log */
    int server_rank,      /* rank of server hosting data */
    int app_id,           /* app_id holding data */
    int client_rank)      /* client rank holding data */
{
    /* offset of first byte in request */
    size_t pos = offset;

    /* offset of last byte in request */
    size_t last_offset = offset + length - 1;

    /* this will track the current offset within the log
     * where the data starts, we advance it with each key
     * we generate depending on the data associated with
     * each key */
    size_t logpos = log_offset;

    /* iterate over slice ranges and generate a start/end
     * pair of keys for each */
    int count = 0;
    while (pos <= last_offset) {
        /* compute offset for first byte in this slice */
        size_t start = pos;

        /* offset for last byte in this slice,
         * assume that's the last byte of the same slice
         * containing start, unless that happens to be
         * beyond the last byte of the actual request */
        size_t start_slice = start / meta_slice_sz;
        size_t end = (start_slice + 1) * meta_slice_sz - 1;
        if (end > last_offset) {
            end = last_offset;
        }

        /* length of extent in this slice */
        size_t len = end - start + 1;

        /* create key to describe this log entry */
        unifyfs_key_t* k = keys[count];
        k->gfid   = gfid;
        k->offset = start;
        keylens[count] = sizeof(unifyfs_key_t);

        /* create value to store address of data */
        unifyfs_val_t* v = vals[count];
        v->addr           = logpos;
        v->len            = len;
        v->app_id         = app_id;
        v->rank           = client_rank;
        v->delegator_rank = server_rank;
        vallens[count] = sizeof(unifyfs_val_t);

        /* advance to next slot in key/value arrays */
        count++;

        /* advance offset into log */
        logpos += len;

        /* advance to first byte offset of next slice */
        pos = end + 1;
    }

    /* return number of keys we generated */
    return count;
}

/* given a global file id, an offset, and a length to read from that
 * file, create keys needed to query MDHIM for location of data
 * corresponding to that extent, returns the number of keys inserted
 * into key array provided by caller */
static int split_request(
    unifyfs_key_t** keys, /* list to add newly created keys into */
    int* keylens,         /* list to add byte size of each key */
    int gfid,             /* target global file id to read from */
    size_t offset,        /* starting offset of read */
    size_t length)        /* number of bytes to read */
{
    /* offset of first byte in request */
    size_t pos = offset;

    /* offset of last byte in request */
    size_t last_offset = offset + length - 1;

    /* iterate over slice ranges and generate a start/end
     * pair of keys for each */
    int count = 0;
    while (pos <= last_offset) {
        /* compute offset for first byte in this segment */
        size_t start = pos;

        /* offset for last byte in this segment,
         * assume that's the last byte of the same segment
         * containing start, unless that happens to be
         * beyond the last byte of the actual request */
        size_t start_slice = start / meta_slice_sz;
        size_t end = (start_slice + 1) * meta_slice_sz - 1;
        if (end > last_offset) {
            end = last_offset;
        }

        /* create key to describe first byte we'll read
         * in this slice */
        keys[count]->gfid   = gfid;
        keys[count]->offset = start;
        keylens[count] = sizeof(unifyfs_key_t);
        count++;

        /* create key to describe last byte we'll read
         * in this slice */
        keys[count]->gfid   = gfid;
        keys[count]->offset = end;
        keylens[count] = sizeof(unifyfs_key_t);
        count++;

        /* advance to first byte offset of next slice */
        pos = end + 1;
    }

    /* return number of keys we generated */
    return count;
}


static int mdhim_init(unifyfs_cfg_t* cfg)
{
    int ret = 0;

    LOGDBG("initializing file operations..");

    ret = meta_init_store(cfg);
    if (ret) {
        LOGERR("failed to initialize the meta kv store (ret=%d)", ret);
    }

    return ret;
}

static int mdhim_metaget(unifyfs_fops_ctx_t* ctx,
                         int gfid, unifyfs_file_attr_t* attr)
{
    return unifyfs_get_file_attribute(gfid, attr);
}

static int mdhim_metaset(unifyfs_fops_ctx_t* ctx,
                         int gfid, int create, unifyfs_file_attr_t* attr)
{
    return unifyfs_set_file_attribute(create, create, attr);
}

static int mdhim_sync(unifyfs_fops_ctx_t* ctx)
{
    size_t i;

    /* assume we'll succeed */
    int ret = (int)UNIFYFS_SUCCESS;

    /* get memory page size on this machine */
    int page_sz = getpagesize();

    /* get application client */
    app_client* client = get_app_client(ctx->app_id, ctx->client_id);
    if (NULL == client) {
        return EINVAL;
    }

    /* get pointer to superblock for this client and app */
    shm_context* super_ctx = client->shmem_super;
    if (NULL == super_ctx) {
        LOGERR("missing client superblock");
        return EIO;
    }
    char* superblk = (char*)(super_ctx->addr);

    /* get pointer to start of key/value region in superblock */
    char* meta = superblk + client->super_meta_offset;

    /* get number of file extent index values client has for us,
     * stored as a size_t value in meta region of shared memory */
    size_t extent_num_entries = *(size_t*)(meta);

    /* indices are stored in the superblock shared memory
     * created by the client, these are stored as index_t
     * structs starting one page size offset into meta region */
    char* ptr_extents = meta + page_sz;

    if (extent_num_entries == 0) {
        /* Nothing to do */
        return UNIFYFS_SUCCESS;
    }

    unifyfs_index_t* meta_payload = (unifyfs_index_t*)(ptr_extents);

    /* total up number of key/value pairs we'll need for this
     * set of index values */
    size_t slices = 0;
    for (i = 0; i < extent_num_entries; i++) {
        size_t offset = meta_payload[i].file_pos;
        size_t length = meta_payload[i].length;
        slices += meta_num_slices(offset, length);
    }
    if (slices >= UNIFYFS_MAX_SPLIT_CNT) {
        LOGERR("Error allocating buffers");
        return ENOMEM;
    }

    /* pointers to memory we'll dynamically allocate for file extents */
    unifyfs_key_t** keys = NULL;
    unifyfs_val_t** vals = NULL;
    int* key_lens        = NULL;
    int* val_lens        = NULL;

    /* allocate storage for file extent key/values */
    /* TODO: possibly get this from memory pool */
    keys     = alloc_key_array(slices);
    vals     = alloc_value_array(slices);
    key_lens = calloc(slices, sizeof(int));
    val_lens = calloc(slices, sizeof(int));
    if ((NULL == keys) ||
        (NULL == vals) ||
        (NULL == key_lens) ||
        (NULL == val_lens)) {
        LOGERR("failed to allocate memory for file extents");
        ret = ENOMEM;
        goto mdhim_sync_exit;
    }

    /* create file extent key/values for insertion into MDHIM */
    int count = 0;
    for (i = 0; i < extent_num_entries; i++) {
        /* get file offset, length, and log offset for this entry */
        unifyfs_index_t* meta = &meta_payload[i];
        int gfid      = meta->gfid;
        size_t offset = meta->file_pos;
        size_t length = meta->length;
        size_t logpos = meta->log_pos;

        /* split this entry at the offset boundaries */
        int used = split_index(
            &keys[count], &vals[count], &key_lens[count], &val_lens[count],
            gfid, offset, length, logpos,
            glb_pmi_rank, ctx->app_id, ctx->client_id);

        /* count up the number of keys we used for this index */
        count += used;
    }

    /* batch insert file extent key/values into MDHIM */
    ret = unifyfs_set_file_extents((int)count,
        keys, key_lens, vals, val_lens);
    if (ret != UNIFYFS_SUCCESS) {
        /* TODO: need proper error handling */
        LOGERR("unifyfs_set_file_extents() failed");
        goto mdhim_sync_exit;
    }

mdhim_sync_exit:
    /* clean up memory */
    if (NULL != keys) {
        free_key_array(keys);
    }

    if (NULL != vals) {
        free_value_array(vals);
    }

    if (NULL != key_lens) {
        free(key_lens);
    }

    if (NULL != val_lens) {
        free(val_lens);
    }

    return ret;
}

/*
 * currently, we publish all key-value pairs (regardless of the @gfid).
 */
static int mdhim_fsync(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return mdhim_sync(ctx);
}

static int mdhim_filesize(unifyfs_fops_ctx_t* ctx, int gfid, size_t* outsize)
{
    size_t filesize = 0;
    int ret = unifyfs_invoke_filesize_rpc(gfid, &filesize);
    if (ret) {
        LOGERR("filesize rpc failed (ret=%d)", ret);
    } else {
        LOGDBG("filesize rpc returned %zu", filesize);
        *outsize = filesize;
    }

    unifyfs_file_attr_t attr = { 0, };
    mdhim_metaget(ctx, gfid, &attr);

    /* return greater of rpc value and mdhim metadata size */
    size_t asize = (size_t) attr.size;
    if (asize > filesize) {
        *outsize = asize;
    }

    return ret;
}

/* delete any key whose last byte is beyond the specified
 * file size */
static int truncate_delete_keys(
    size_t filesize,           /* new file size */
    int num,                   /* number of entries in keyvals */
    unifyfs_keyval_t* keyvals) /* list of existing key/values */
{
    /* assume we'll succeed */
    int ret = (int) UNIFYFS_SUCCESS;

    /* pointers to memory we'll dynamically allocate for file extents */
    unifyfs_key_t** unifyfs_keys = NULL;
    unifyfs_val_t** unifyfs_vals = NULL;
    int* unifyfs_key_lens        = NULL;
    int* unifyfs_val_lens        = NULL;

    /* in the worst case, we'll have to delete all existing keys */
    /* allocate storage for file extent key/values */
    /* TODO: possibly get this from memory pool */
    unifyfs_keys     = alloc_key_array(num);
    unifyfs_vals     = alloc_value_array(num);
    unifyfs_key_lens = calloc(num, sizeof(int));
    unifyfs_val_lens = calloc(num, sizeof(int));
    if ((NULL == unifyfs_keys) ||
        (NULL == unifyfs_vals) ||
        (NULL == unifyfs_key_lens) ||
        (NULL == unifyfs_val_lens)) {
        LOGERR("failed to allocate memory for file extents");
        ret = ENOMEM;
        goto truncate_delete_exit;
    }

    /* counter for number of key/values we need to delete */
    int delete_count = 0;

    /* iterate over each key, and if this index extends beyond desired
     * file size, create an entry to delete that key */
    int i;
    for (i = 0; i < num; i++) {
        /* get pointer to next key value pair */
        unifyfs_keyval_t* kv = &keyvals[i];

        /* get last byte offset for this segment of the file */
        size_t last_offset = kv->key.offset + kv->val.len;

        /* if this segment extends beyond the new file size,
         * we need to delete this index entry */
        if (last_offset > filesize) {
            /* found an index that extends past end of desired
             * file size, get next empty key entry from the pool */
            unifyfs_key_t* key = unifyfs_keys[delete_count];

            /* define the key to be deleted */
            key->gfid   = kv->key.gfid;
            key->offset = kv->key.offset;

            /* MDHIM needs to know the byte size of each key and value */
            unifyfs_key_lens[delete_count] = sizeof(unifyfs_key_t);
            //unifyfs_val_lens[delete_count] = sizeof(unifyfs_val_t);

            /* increment the number of keys we're deleting */
            delete_count++;
        }
    }

    /* batch delete file extent key/values from MDHIM */
    if (delete_count > 0) {
        ret = unifyfs_delete_file_extents(delete_count,
            unifyfs_keys, unifyfs_key_lens);
        if (ret != UNIFYFS_SUCCESS) {
            /* TODO: need proper error handling */
            LOGERR("unifyfs_delete_file_extents() failed");
            goto truncate_delete_exit;
        }
    }

truncate_delete_exit:
    /* clean up memory */

    if (NULL != unifyfs_keys) {
        free_key_array(unifyfs_keys);
    }

    if (NULL != unifyfs_vals) {
        free_value_array(unifyfs_vals);
    }

    if (NULL != unifyfs_key_lens) {
        free(unifyfs_key_lens);
    }

    if (NULL != unifyfs_val_lens) {
        free(unifyfs_val_lens);
    }

    return ret;
}

/* rewrite any key that overlaps with new file size,
 * we assume the existing key has already been deleted */
static int truncate_rewrite_keys(
    size_t filesize,           /* new file size */
    int num,                   /* number of entries in keyvals */
    unifyfs_keyval_t* keyvals) /* list of existing key/values */
{
    /* assume we'll succeed */
    int ret = (int) UNIFYFS_SUCCESS;

    /* pointers to memory we'll dynamically allocate for file extents */
    unifyfs_key_t** unifyfs_keys = NULL;
    unifyfs_val_t** unifyfs_vals = NULL;
    int* unifyfs_key_lens        = NULL;
    int* unifyfs_val_lens        = NULL;

    /* in the worst case, we'll have to rewrite all existing keys */
    /* allocate storage for file extent key/values */
    /* TODO: possibly get this from memory pool */
    unifyfs_keys     = alloc_key_array(num);
    unifyfs_vals     = alloc_value_array(num);
    unifyfs_key_lens = calloc(num, sizeof(int));
    unifyfs_val_lens = calloc(num, sizeof(int));
    if ((NULL == unifyfs_keys) ||
        (NULL == unifyfs_vals) ||
        (NULL == unifyfs_key_lens) ||
        (NULL == unifyfs_val_lens)) {
        LOGERR("failed to allocate memory for file extents");
        ret = ENOMEM;
        goto truncate_rewrite_exit;
    }

    /* counter for number of key/values we need to rewrite */
    int count = 0;

    /* iterate over each key, and if this index starts before
     * and ends after the desired file size, create an entry
     * that ends at new file size */
    int i;
    for (i = 0; i < num; i++) {
        /* get pointer to next key value pair */
        unifyfs_keyval_t* kv = &keyvals[i];

        /* get first byte offset for this segment of the file */
        size_t first_offset = kv->key.offset;

        /* get last byte offset for this segment of the file */
        size_t last_offset = kv->key.offset + kv->val.len;

        /* if this segment extends beyond the new file size,
         * we need to rewrite this index entry */
        if (first_offset < filesize &&
            last_offset  > filesize) {
            /* found an index that overlaps end of desired
             * file size, get next empty key entry from the pool */
            unifyfs_key_t* key = unifyfs_keys[count];

            /* define the key to be rewritten */
            key->gfid   = kv->key.gfid;
            key->offset = kv->key.offset;

            /* compute new length of this entry */
            size_t newlen = (size_t)(filesize - first_offset);

            /* for the value, we store the log position, the length,
             * the host server (delegator rank), the mount point id
             * (app id), and the client id (rank) */
            unifyfs_val_t* val = unifyfs_vals[count];
            val->addr           = kv->val.addr;
            val->len            = newlen;
            val->delegator_rank = kv->val.delegator_rank;
            val->app_id         = kv->val.app_id;
            val->rank           = kv->val.rank;

            /* MDHIM needs to know the byte size of each key and value */
            unifyfs_key_lens[count] = sizeof(unifyfs_key_t);
            unifyfs_val_lens[count] = sizeof(unifyfs_val_t);

            /* increment the number of keys we're deleting */
            count++;
        }
    }

    /* batch set file extent key/values from MDHIM */
    if (count > 0) {
        ret = unifyfs_set_file_extents(count,
            unifyfs_keys, unifyfs_key_lens,
            unifyfs_vals, unifyfs_val_lens);
        if (ret != UNIFYFS_SUCCESS) {
            /* TODO: need proper error handling */
            LOGERR("unifyfs_set_file_extents() failed");
            goto truncate_rewrite_exit;
        }
    }

truncate_rewrite_exit:
    /* clean up memory */

    if (NULL != unifyfs_keys) {
        free_key_array(unifyfs_keys);
    }

    if (NULL != unifyfs_vals) {
        free_value_array(unifyfs_vals);
    }

    if (NULL != unifyfs_key_lens) {
        free(unifyfs_key_lens);
    }

    if (NULL != unifyfs_val_lens) {
        free(unifyfs_val_lens);
    }

    return ret;
}

static int mdhim_truncate(unifyfs_fops_ctx_t* ctx, int gfid, off_t len)
{
    size_t newsize = (size_t) len;

    /* set offset and length to request *all* key/value pairs
     * for this file */
    size_t offset = 0;

    /* want to pick the highest integer offset value a file
     * could have here */
    size_t length = (SIZE_MAX >> 1) - 1;

    /* get the locations of all the read requests from the
     * key-value store*/
    unifyfs_key_t key1, key2;

    /* create key to describe first byte we'll read */
    key1.gfid   = gfid;
    key1.offset = offset;

    /* create key to describe last byte we'll read */
    key2.gfid   = gfid;
    key2.offset = offset + length - 1;

    /* set up input params to specify range lookup */
    unifyfs_key_t* unifyfs_keys[2] = {&key1, &key2};
    int key_lens[2] = {sizeof(unifyfs_key_t), sizeof(unifyfs_key_t)};

    /* look up all entries in this range */
    int num_vals = 0;
    unifyfs_keyval_t* keyvals = NULL;
    int rc = unifyfs_get_file_extents(2, unifyfs_keys, key_lens,
                                      &num_vals, &keyvals);
    if (UNIFYFS_SUCCESS != rc) {
        /* failed to look up extents, bail with error */
        return UNIFYFS_FAILURE;
    }

    /* compute our file size by iterating over each file
     * segment and taking the max logical offset */
    int i;
    size_t filesize = 0;
    for (i = 0; i < num_vals; i++) {
        /* get pointer to next key value pair */
        unifyfs_keyval_t* kv = &keyvals[i];

        /* get last byte offset for this segment of the file */
        size_t last_offset = kv->key.offset + kv->val.len;

        /* update our filesize if this offset is bigger than the current max */
        if (last_offset > filesize) {
            filesize = last_offset;
        }
    }

    /* get filesize as recorded in metadata, which may be bigger if
     * user issued an ftruncate on the file to extend it past the
     * last write */
    size_t filesize_meta = filesize;

    /* given the global file id, look up file attributes
     * from key/value store */
    unifyfs_file_attr_t fattr;
    rc = unifyfs_get_file_attribute(gfid, &fattr);
    if (rc == UNIFYFS_SUCCESS) {
        /* found file attribute for this file, now get its size */
        filesize_meta = fattr.size;
    } else {
        /* failed to find file attributes for this file */
        goto truncate_exit;
    }

    /* take maximum of last write and file size from metadata */
    if (filesize_meta > filesize) {
        filesize = filesize_meta;
    }

    /* may need to throw away and rewrite keys if shrinking file */
    if (newsize < filesize) {
        /* delete any key that extends beyond new file size */
        rc = truncate_delete_keys(newsize, num_vals, keyvals);
        if (rc != UNIFYFS_SUCCESS) {
            goto truncate_exit;
        }

        /* rewrite any key that overlaps new file size */
        rc = truncate_rewrite_keys(newsize, num_vals, keyvals);
        if (rc != UNIFYFS_SUCCESS) {
            goto truncate_exit;
        }
    }

    /* update file size field with latest size */
    fattr.size = newsize;
    rc = unifyfs_set_file_attribute(1, 0, &fattr);
    if (rc != UNIFYFS_SUCCESS) {
        /* failed to update file attributes with new file size */
        goto truncate_exit;
    }

    rc = unifyfs_invoke_truncate_rpc(gfid, newsize);
    if (rc) {
        LOGERR("truncate rpc failed");
    }

truncate_exit:

    /* free off key/value buffer returned from get_file_extents */
    if (NULL != keyvals) {
        free(keyvals);
        keyvals = NULL;
    }

    return rc;
}

static int mdhim_laminate(unifyfs_fops_ctx_t* ctx, int gfid)
{
    int rc = UNIFYFS_SUCCESS;

    /* given the global file id, look up file attributes
     * from key/value store */
    unifyfs_file_attr_t attr = { 0, };
    int ret = mdhim_metaget(ctx, gfid, &attr);
    if (ret != UNIFYFS_SUCCESS) {
        /* failed to find attributes for the file */
        return ret;
    }

    /* if item is not a file, bail with error */
    mode_t mode = (mode_t) attr.mode;
    if ((mode & S_IFMT) != S_IFREG) {
        /* item is not a regular file */
        LOGERR("ERROR: only regular files can be laminated (gfid=%d)", gfid);
        return EINVAL;
    }

    /* lookup current file size */
    size_t filesize;
    ret = mdhim_filesize(ctx, gfid, &filesize);
    if (ret != UNIFYFS_SUCCESS) {
        /* failed to get file size for file */
        LOGERR("lamination file size calculation failed (gfid=%d)", gfid);
        return ret;
    }

    /* update fields in metadata */
    attr.size         = filesize;
    attr.is_laminated = 1;

    /* update metadata, set size and laminate */
    rc = unifyfs_set_file_attribute(1, 1, &attr);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("lamination metadata update failed (gfid=%d)", gfid);
    }

    return rc;
}

static int mdhim_unlink(unifyfs_fops_ctx_t* ctx, int gfid)
{
    int rc = UNIFYFS_SUCCESS;

    /* given the global file id, look up file attributes
     * from key/value store */
    unifyfs_file_attr_t attr;
    int ret = unifyfs_get_file_attribute(gfid, &attr);
    if (ret != UNIFYFS_SUCCESS) {
        /* failed to find attributes for the file */
        return ret;
    }

    /* if item is a file, call truncate to free space */
    mode_t mode = (mode_t) attr.mode;
    if ((mode & S_IFMT) == S_IFREG) {
        /* item is regular file, truncate to 0 */
        ret = mdhim_truncate(ctx, gfid, 0);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to delete write extents for file,
             * let's leave the file attributes in place */
            return ret;
        }
    }

    /* delete metadata */
    ret = unifyfs_delete_file_attribute(gfid);
    if (ret != UNIFYFS_SUCCESS) {
        rc = ret;
    }

    rc = unifyfs_invoke_unlink_rpc(gfid);
    if (rc) {
        LOGERR("unlink rpc failed (ret=%d)", rc);
    }

    return rc;
}


/* given a set of input key pairs, where each pair describes the first
 * and last byte offset of a data range, refer to our local extent map
 * and generate keyval responses for any ranges covering data that is
 * local to the server, generate new key pairs to describe remaining
 * holes that will be queried against the global key/value store,
 * the list of output keys, key lengths, and keyvals are allocated
 * and returned to be freed by the caller */
static int get_local_keyvals(
    int num_keys,               /* number of input keys */
    unifyfs_key_t** keys,       /* list of input keys */
    int* keylens,               /* list of input key lengths */
    int* out_global,            /* number of output keys for server */
    unifyfs_key_t*** out_keys,  /* list of output keys */
    int** out_keylens,          /* list of output key lengths */
    int* num_keyvals,           /* number of output keyvals from local data */
    unifyfs_keyval_t** keyvals) /* list of output keyvals */
{
    /* initialize output parameters */
    *out_global  = 0;
    *out_keys    = NULL;
    *out_keylens = NULL;
    *num_keyvals = 0;
    *keyvals     = NULL;

    /* allocate memory to copy key/value data */
    int max_keyvals = UNIFYFS_MAX_SPLIT_CNT;
    unifyfs_keyval_t* kvs_local = (unifyfs_keyval_t*) calloc(
        max_keyvals, sizeof(unifyfs_keyval_t));
    if (NULL == kvs_local) {
        LOGERR("failed to allocate keyvals");
        return (int)UNIFYFS_ERROR_MDHIM;
    }

    /* allocate memory to define remaining keys to
     * search in global store */
    unifyfs_key_t** keys_global = alloc_key_array(max_keyvals);
    if (NULL == keys_global) {
        LOGERR("failed to allocate keys");
        free(kvs_local);
        return (int)UNIFYFS_ERROR_MDHIM;
    }

    /* allocate memory to define key lengths for remaining keys to
     * search in global store */
    int* keylens_global = (int*) calloc(max_keyvals, sizeof(int));
    if (NULL == keylens_global) {
        LOGERR("failed to allocate keylens");
        free_key_array(keys_global);
        free(kvs_local);
        return (int)UNIFYFS_ERROR_MDHIM;
    }

    /* counters for the number of local keyvals we create and the
     * number of keys we generate for the global key/value store */
    int count_global = 0;
    int count_local  = 0;

    int i;
    for (i = 0; i < num_keys; i += 2) {
        /* get next key pair that describe start and end offsets */
        unifyfs_key_t* k1 = keys[i+0];
        unifyfs_key_t* k2 = keys[i+1];

        /* get gfid, start, and end offset of this pair */
        int gfid     = k1->gfid;
        size_t start = k1->offset;
        size_t end   = k2->offset;

        /* we'll define key/values in these temp arrays that correspond
         * to extents we have locally */
        unifyfs_key_t tmpkeys[UNIFYFS_MAX_SPLIT_CNT];
        unifyfs_val_t tmpvals[UNIFYFS_MAX_SPLIT_CNT];

        /* look up any entries we can find in our local extent map */
        int num_local = 0;
        int ret = unifyfs_inode_span_extents(gfid, start, end,
                UNIFYFS_MAX_SPLIT_CNT, tmpkeys, tmpvals, &num_local);
        if (ret) {
            LOGERR("failed to span extents (gfid=%d)", gfid);
            // now what?
        }

        /* iterate over local keys, create new keys to pass to server
         * for any holes in our local extents */
        int j;
        size_t nextstart = start;
        for (j = 0; j < num_local; j++) {
            /* get next key/value returned from local extent */
            unifyfs_key_t* k = &tmpkeys[j];
            unifyfs_val_t* v = &tmpvals[j];

            /* if we have a gap in our data,
             * we need to ask the global key/value store */
            if (nextstart < k->offset) {
                /* we're missing a section of bytes, so create a key
                 * pair to search for this hole in the global key/value
                 * store */

                /* check that we don't overflow the global array */
                if (count_global + 2 > max_keyvals) {
                    /* exhausted our space */
                    free(keylens_global);
                    free_key_array(keys_global);
                    free(kvs_local);
                    return ENOMEM;
                }

                /* first key is for starting offset of the hole,
                 * which is defined in next start */
                unifyfs_key_t* gk1 = keys_global[count_global];
                gk1->gfid   = gfid;
                gk1->offset = nextstart;
                keylens_global[count_global] = sizeof(unifyfs_key_t);
                count_global++;

                /* second key is for ending offset of the hole,
                 * which will be the offset of the byte that comes
                 * just before the offset of the current key */
                unifyfs_key_t* gk2 = keys_global[count_global];
                gk2->gfid   = gfid;
                gk2->offset = k->offset - 1;
                keylens_global[count_global] = sizeof(unifyfs_key_t);
                count_global++;
            } else {
                /* otherwise we have a local extent that matches,
                 * copy the corresponding key/value pair into the
                 * local output array */

                /* check that we don't overflow the local array */
                if (count_local + 1 > max_keyvals) {
                    /* exhausted our space */
                    free(keylens_global);
                    free_key_array(keys_global);
                    free(kvs_local);
                    return ENOMEM;
                }

                /* create a key/value describing the
                 * current local extent */

                /* get pointer to next key/val */
                unifyfs_keyval_t* kv = &kvs_local[count_local];

                /* copy in the key and value generated from the call
                 * to tree_span into our array of local key/value pairs */
                memcpy(&kv->key, k, sizeof(unifyfs_key_t));
                memcpy(&kv->val, v, sizeof(unifyfs_val_t));

                /* increase the number of keyvals we've found locally */
                count_local++;
            }

            /* advance to start of next segment we're looking for */
            nextstart = k->offset + v->len;
        }

        /* verify that we covered the full range, create a key pair
         * to look in the global key/value store for any trailing hole */
        if (nextstart <= end) {
            /* check that we don't overflow the global array */
            if (count_global + 2 > max_keyvals) {
                /* exhausted our space */
                free(keylens_global);
                free_key_array(keys_global);
                free(kvs_local);
                return ENOMEM;
            }

            /* first key is for starting offset of the hole,
             * which is defined in next start */
            unifyfs_key_t* gk1 = keys_global[count_global];
            gk1->gfid   = gfid;
            gk1->offset = nextstart;
            keylens_global[count_global] = sizeof(unifyfs_key_t);
            count_global++;

            /* second key is for ending offset of the hole */
            unifyfs_key_t* gk2 = keys_global[count_global];
            gk2->gfid   = gfid;
            gk2->offset = end;
            keylens_global[count_global] = sizeof(unifyfs_key_t);
            count_global++;
        }
    }

    /* set output values */
    *out_global  = count_global;
    *out_keys    = keys_global;
    *out_keylens = keylens_global;
    *num_keyvals = count_local;
    *keyvals     = kvs_local;

    return UNIFYFS_SUCCESS;
}

static int create_gfid_chunk_reads(reqmgr_thrd_t* thrd_ctrl, int gfid,
                                   int app_id, int client_id, int num_keys,
                                   unifyfs_key_t** keys, int* keylens)
{
    int rc = UNIFYFS_SUCCESS;

    int num_vals = 0;
    unifyfs_keyval_t* keyvals = NULL;

    if (!unifyfs_local_extents) {
        /* not using our local extent map,
         * lookup all keys from global key/value store */
        rc = unifyfs_get_file_extents(num_keys, keys, keylens,
            &num_vals, &keyvals);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to lookup keyvals from global key/val store");
            return rc;
        }
    } else {
        /* lookup entries from local key/value map first,
         * for any missing gaps, create new keys to search in global map */
        int global_num_keys = 0;
        int local_num       = 0;
        unifyfs_key_t** global_keys     = NULL;
        int* global_keylens             = NULL;
        unifyfs_keyval_t* local_keyvals = NULL;
        rc = get_local_keyvals(num_keys, keys, keylens,
            &global_num_keys, &global_keys, &global_keylens,
            &local_num, &local_keyvals);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to lookup keyvals from local extents");
            return rc;
        }

        /* now lookup remaining keys in global key/value store */
        int global_num = 0;
        unifyfs_keyval_t* global_keyvals = NULL;
        if (global_num_keys > 0) {
            rc = unifyfs_get_file_extents(
                global_num_keys, global_keys, global_keylens,
                &global_num, &global_keyvals);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("failed to lookup keyvals from global key/val store");
                free_key_array(global_keys);
                free(global_keylens);
                free(local_keyvals);
                return rc;
            }
        }

        /* total up number of keyvals we have from local and global
         * server combined */
        num_vals = local_num + global_num;

        /* allocate space to combine local and global keyvals */
        keyvals = (unifyfs_keyval_t*) calloc(
            num_vals, sizeof(unifyfs_keyval_t));
        if (NULL == keyvals) {
            LOGERR("failed to allocate keyvals");
            free_key_array(global_keys);
            free(global_keylens);
            free(local_keyvals);
            free(global_keyvals);
            return (int)UNIFYFS_ERROR_MDHIM;
        }

        /* merge local and global keyvals into one array,
         * copy in local keyvals first */
        memcpy(&keyvals[0], local_keyvals,
            local_num * sizeof(unifyfs_keyval_t));

        /* copy global keyvals into array after any local keyvals */
        memcpy(&keyvals[local_num], global_keyvals,
            global_num * sizeof(unifyfs_keyval_t));

        /* free memory allocated during lookup functions */
        free_key_array(global_keys);
        free(global_keylens);
        free(local_keyvals);
        free(global_keyvals);
    }

    /* this is to maintain limits imposed in previous code
     * that would throw fatal errors */
    if (num_vals >= UNIFYFS_MAX_SPLIT_CNT ||
        num_vals >= MAX_META_PER_SEND) {
        LOGERR("too many key/values returned in range lookup");
        if (NULL != keyvals) {
            free(keyvals);
            keyvals = NULL;
        }
        return ENOMEM;
    }

    if (UNIFYFS_SUCCESS != rc) {
        /* failed to find any key / value pairs */
        rc = UNIFYFS_FAILURE;
    } else {
        /* if we get more than one write index entry
         * sort them by file id and then by delegator rank */
        if (num_vals > 1) {
            qsort(keyvals, (size_t)num_vals, sizeof(unifyfs_keyval_t),
                  unifyfs_keyval_compare);
        }

        server_read_req_t* rdreq = rm_reserve_read_req(thrd_ctrl);
        if (NULL == rdreq) {
            rc = UNIFYFS_FAILURE;
        } else {
            rdreq->app_id         = app_id;
            rdreq->client_id      = client_id;
            rdreq->extent.gfid    = gfid;
            rdreq->extent.errcode = EINPROGRESS;

            rc = rm_create_chunk_requests(thrd_ctrl, rdreq,
                                          num_vals, keyvals);
            if (rc != (int)UNIFYFS_SUCCESS) {
                rm_release_read_req(thrd_ctrl, rdreq);
            }
        }
    }

    /* free off key/value buffer returned from get_file_extents */
    if (NULL != keyvals) {
        free(keyvals);
        keyvals = NULL;
    }

    return rc;
}

static int mdhim_read(unifyfs_fops_ctx_t* ctx,
                      int gfid, off_t offset, size_t length)
{
    /* get application client */
    int app_id = ctx->app_id;
    int client_id = ctx->client_id;
    app_client* client = get_app_client(app_id, client_id);
    if (NULL == client) {
        return (int)UNIFYFS_FAILURE;
    }

    /* get thread control structure */
    reqmgr_thrd_t* thrd_ctrl = client->reqmgr;

    /* get chunks corresponding to requested client read extent
     *
     * Generate a pair of keys for the read request, representing the start
     * and end offset. MDHIM returns all key-value pairs that fall within
     * the offset range.
     *
     * TODO: this is specific to the MDHIM in the source tree and not portable
     *       to other KV-stores. This needs to be revisited to utilize some
     *       other mechanism to retrieve all relevant key-value pairs from the
     *       KV-store.
     */

    /* count number of slices this range covers */
    size_t slices = meta_num_slices(offset, length);
    if (slices >= UNIFYFS_MAX_SPLIT_CNT) {
        LOGERR("Error allocating buffers");
        return ENOMEM;
    }

    /* allocate key storage */
    size_t key_cnt = slices * 2;
    unifyfs_key_t** keys = alloc_key_array(key_cnt);
    int* key_lens = (int*) calloc(key_cnt, sizeof(int));
    if ((NULL == keys) ||
        (NULL == key_lens)) {
        // this is a fatal error
        // TODO: we need better error handling
        LOGERR("Error allocating buffers");
        return ENOMEM;
    }

    /* split range of read request at boundaries used for
     * MDHIM range query */
    split_request(keys, key_lens, gfid, offset, length);

    /* queue up the read operations */
    int rc = create_gfid_chunk_reads(thrd_ctrl, gfid, app_id, client_id,
                                     key_cnt, keys, key_lens);

    /* free memory allocated for key storage */
    free_key_array(keys);
    free(key_lens);

    return rc;
}

static int mdhim_mread(unifyfs_fops_ctx_t* ctx, size_t num_req, void* reqbuf)
{
    int rc = UNIFYFS_SUCCESS;
    int app_id = ctx->app_id;
    int client_id = ctx->client_id;

    /* get application client */
    app_client* client = get_app_client(app_id, client_id);
    if (NULL == client) {
        return (int)UNIFYFS_FAILURE;
    }

    /* get thread control structure */
    reqmgr_thrd_t* thrd_ctrl = client->reqmgr;

     /* get the locations of all the read requests from the key-value store */
    unifyfs_ReadRequest_table_t readRequest =
        unifyfs_ReadRequest_as_root(reqbuf);
    unifyfs_Extent_vec_t extents = unifyfs_ReadRequest_extents(readRequest);
    size_t extents_len = unifyfs_Extent_vec_len(extents);
    assert(extents_len == num_req);

    /* count up number of slices these request cover */
    int j;
    size_t slices = 0;
    for (j = 0; j < num_req; j++) {
        /* get offset and length of next request */
        size_t off = unifyfs_Extent_offset(unifyfs_Extent_vec_at(extents, j));
        size_t len = unifyfs_Extent_length(unifyfs_Extent_vec_at(extents, j));

        /* add in number of slices this request needs */
        slices += meta_num_slices(off, len);
    }
    if (slices >= UNIFYFS_MAX_SPLIT_CNT) {
        LOGERR("Error allocating buffers");
        return ENOMEM;
    }

    /* allocate key storage */
    size_t key_cnt = slices * 2;
    unifyfs_key_t** keys = alloc_key_array(key_cnt);
    int* key_lens = (int*) calloc(key_cnt, sizeof(int));
    if ((NULL == keys) ||
        (NULL == key_lens)) {
        // this is a fatal error
        // TODO: we need better error handling
        LOGERR("Error allocating buffers");
        return ENOMEM;
    }

    /* get chunks corresponding to requested client read extents */
    int ret;
    int num_keys = 0;
    int last_gfid = -1;
    for (j = 0; j < num_req; j++) {
        /* get the file id for this request */
        int gfid = unifyfs_Extent_fid(unifyfs_Extent_vec_at(extents, j));

        /* if we have switched to a different file, create chunk reads
         * for the previous file */
        if (j && (gfid != last_gfid)) {
            /* create requests for all extents of last_gfid */
            ret = create_gfid_chunk_reads(thrd_ctrl, last_gfid,
                                          app_id, client_id,
                                          num_keys, keys, key_lens);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("Error creating chunk reads for gfid=%d", last_gfid);
                rc = ret;
            }

            /* reset key counter for the current gfid */
            num_keys = 0;
        }

        /* get offset and length of current read request */
        size_t off = unifyfs_Extent_offset(unifyfs_Extent_vec_at(extents, j));
        size_t len = unifyfs_Extent_length(unifyfs_Extent_vec_at(extents, j));
        LOGDBG("gfid:%d, offset:%zu, length:%zu", gfid, off, len);

        /* Generate a pair of keys for each read request, representing
         * the start and end offsets. MDHIM returns all key-value pairs that
         * fall within the offset range.
         *
         * TODO: this is specific to the MDHIM in the source tree and not
         *       portable to other KV-stores. This needs to be revisited to
         *       utilize some other mechanism to retrieve all relevant KV
         *       pairs from the KV-store.
         */

        /* split range of read request at boundaries used for
         * MDHIM range query */
        int used = split_request(&keys[num_keys], &key_lens[num_keys],
            gfid, off, len);
        num_keys += used;

        /* keep track of the last gfid value that we processed */
        last_gfid = gfid;
    }

    /* create requests for all extents of final gfid */
    ret = create_gfid_chunk_reads(thrd_ctrl, last_gfid,
        app_id, client_id, num_keys, keys, key_lens);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("Error creating chunk reads for gfid=%d", last_gfid);
        rc = ret;
    }

    /* free memory allocated for key storage */
    free_key_array(keys);
    free(key_lens);

    return rc;
}

static struct unifyfs_fops _fops_mdhim = {
    .name = "mdhim",
    .init = mdhim_init,
    .metaget = mdhim_metaget,
    .metaset = mdhim_metaset,
    .sync = mdhim_sync,
    .fsync = mdhim_fsync,
    .filesize = mdhim_filesize,
    .truncate = mdhim_truncate,
    .laminate = mdhim_laminate,
    .unlink = mdhim_unlink,
    .read = mdhim_read,
    .mread = mdhim_mread,
};

struct unifyfs_fops* unifyfs_fops_impl = &_fops_mdhim;

