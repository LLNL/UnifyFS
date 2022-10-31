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
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

#include "unifyfs.h"
#include "unifyfs-internal.h"
#include "unifyfs_fid.h"
#include "client_read.h"

// client-server rpc headers
#include "unifyfs_client_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"

#ifdef USE_SPATH
#include "spath.h"
#endif /* USE_SPATH */

/* list of local clients */
static arraylist_t* client_list; /* = NULL */


/* lock access to shared data structures in superblock */
int unifyfs_stack_lock(unifyfs_client* client)
{
    return 0;
}

/* unlock access to shared data structures in superblock */
int unifyfs_stack_unlock(unifyfs_client* client)
{
    return 0;
}

/* get current file size. if we have a local file corresponding to the
 * given gfid, we use the local metadata. otherwise, we use a global
 * metadata lookup */
off_t unifyfs_gfid_filesize(unifyfs_client* client,
                            int gfid)
{
    off_t filesize = (off_t)-1;

    /* see if we have a fid for this gfid */
    int fid = unifyfs_fid_from_gfid(client, gfid);
    if (fid >= 0) {
        /* got a fid, look up file size through that
         * method, since it may avoid a server rpc call */
        filesize = unifyfs_fid_logical_size(client, fid);
    } else {
        /* no fid for this gfid,
         * look it up with server rpc */
        size_t size;
        int ret = invoke_client_filesize_rpc(client, gfid, &size);
        if (ret == UNIFYFS_SUCCESS) {
            /* got the file size successfully */
            filesize = size;
        }
    }

    return filesize;
}

/*
 * Set the metadata values for a file (after optionally creating it).
 * The gfid for the file is in f_meta->gfid.
 *
 * gfid:   The global file id on which to set metadata.
 *
 * op:     If set to FILE_ATTR_OP_CREATE, attempt to create the file first.
 *         If the file already exists, then update its metadata with the values
 *         from fid filemeta.  If not creating and the file does not exist,
 *         then the server will return an error.
 *
 * gfattr: The metadata values to store.
 */
int unifyfs_set_global_file_meta(unifyfs_client* client,
                                 int gfid,
                                 unifyfs_file_attr_op_e attr_op,
                                 unifyfs_file_attr_t* gfattr)
{
    /* check that we have an input buffer */
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* force the gfid field value to match the gfid we're
     * submitting this under */
    gfattr->gfid = gfid;

    /* send file attributes to server */
    int ret = invoke_client_metaset_rpc(client, attr_op, gfattr);
    return ret;
}

int unifyfs_get_global_file_meta(unifyfs_client* client,
                                 int gfid,
                                 unifyfs_file_attr_t* gfattr)
{
    /* check that we have an output buffer to write to */
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* attempt to lookup file attributes in key/value store */
    unifyfs_file_attr_t fmeta;
    int ret = invoke_client_metaget_rpc(client, gfid, &fmeta);
    if (ret == UNIFYFS_SUCCESS) {
        /* found it, copy attributes to output struct */
        *gfattr = fmeta;
    }
    return ret;
}

/* -------------
 * static APIs
 * ------------- */

/* The super block is a region of shared memory that is used to
 * persist file system meta data.  It also contains a fixed-size
 * region for keeping log index entries for each file.
 *
 *  - stack of free local file ids of length client->max_files,
 *    the local file id is used to index into other data
 *    structures
 *
 *  - array of unifyfs_filename structs, indexed by local
 *    file id, provides a field indicating whether file
 *    slot is in use and if so, the current file name
 *
 *  - array of unifyfs_filemeta structs, indexed by local
 *    file id
 *
 *  - count of number of active index entries
 *  - array of index metadata to track physical offset
 *    of logical file data, of length max_write_index_entries,
 *    entries added during write operations
 */

/* compute memory size of superblock in bytes,
 * critical to keep this consistent with
 * init_superblock_pointers */
static size_t get_superblock_size(unifyfs_client* client)
{
    size_t sb_size = 0;

    /* header: uint32_t to hold magic number to indicate
     * that superblock is initialized */
    sb_size += sizeof(uint32_t);

    /* free file id stack */
    sb_size += unifyfs_stack_bytes(client->max_files);

    /* file name struct array */
    sb_size += client->max_files * sizeof(unifyfs_filename_t);

    /* file metadata struct array */
    sb_size += client->max_files * sizeof(unifyfs_filemeta_t);

    /* index region size */
    sb_size += get_page_size();
    sb_size += client->max_write_index_entries * sizeof(unifyfs_index_t);

    /* return number of bytes */
    return sb_size;
}

static inline
char* next_page_align(char* ptr)
{
    size_t pgsz = get_page_size();
    intptr_t orig = (intptr_t) ptr;
    intptr_t aligned = orig;
    intptr_t offset = orig % pgsz;
    if (offset) {
        aligned += (pgsz - offset);
    }
    LOGDBG("orig=0x%p, next-page-aligned=0x%p", ptr, (char*)aligned);
    return (char*) aligned;
}

/* initialize our global pointers into the given superblock */
static void init_superblock_pointers(unifyfs_client* client,
                                     void* superblock)
{
    char* super = (char*) superblock;
    char* ptr = super;

    /* jump over header (right now just a uint32_t to record
     * magic value of 0xdeadbeef if initialized */
    ptr += sizeof(uint32_t);

    /* stack to manage free file ids */
    client->free_fid_stack = ptr;
    ptr += unifyfs_stack_bytes(client->max_files);

    /* record list of file names */
    client->unifyfs_filelist = (unifyfs_filename_t*)ptr;
    ptr += client->max_files * sizeof(unifyfs_filename_t);

    /* array of file meta data structures */
    client->unifyfs_filemetas = (unifyfs_filemeta_t*)ptr;
    ptr += client->max_files * sizeof(unifyfs_filemeta_t);

    /* record pointers to number of index entries and entries array */
    size_t pgsz = get_page_size();
    size_t entries_size =
        client->max_write_index_entries * sizeof(unifyfs_index_t);
    size_t index_size = pgsz + entries_size;

    client->state.write_index.index_offset = (size_t)(ptr - super);
    client->state.write_index.index_size = index_size;
    client->state.write_index.ptr_num_entries = (size_t*)ptr;
    ptr += pgsz;
    client->state.write_index.index_entries = (unifyfs_index_t*)ptr;
    ptr += client->max_write_index_entries * sizeof(unifyfs_index_t);

    /* compute size of memory we're using and check that
     * it matches what we allocated */
    size_t ptr_size = (size_t)(ptr - (char*)superblock);
    if (ptr_size > client->state.shm_super_ctx->size) {
        LOGERR("Data structures in superblock extend beyond its size");
    }
}

/* initialize data structures for first use */
static int init_superblock_structures(unifyfs_client* client)
{
    for (int i = 0; i < client->max_files; i++) {
        /* indicate that file id is not in use by setting flag to 0 */
        client->unifyfs_filelist[i].in_use = 0;
        client->unifyfs_filelist[i].filename[0] = 0;
    }

    /* initialize stack of free file ids */
    unifyfs_stack_init(client->free_fid_stack, client->max_files);

    /* initialize count of key/value entries */
    *(client->state.write_index.ptr_num_entries) = 0;

    LOGDBG("Meta-stacks initialized!");

    return UNIFYFS_SUCCESS;
}

/* create superblock of specified size and name, or attach to existing
 * block if available */
static int init_superblock_shm(unifyfs_client* client,
                               size_t super_sz)
{
    char shm_name[SHMEM_NAME_LEN] = {0};

    /* attach shmem region for client's superblock */
    sprintf(shm_name, SHMEM_SUPER_FMTSTR,
            client->state.app_id, client->state.client_id);
    shm_context* shm_ctx = unifyfs_shm_alloc(shm_name, super_sz);
    if (NULL == shm_ctx) {
        LOGERR("Failed to attach to shmem superblock region %s", shm_name);
        return UNIFYFS_ERROR_SHMEM;
    }
    client->state.shm_super_ctx = shm_ctx;

    /* init our global variables to point to spots in superblock */
    void* addr = shm_ctx->addr;
    init_superblock_pointers(client, addr);

    /* initialize structures in superblock if it's newly allocated,
     * we depend on shm_open setting all bytes to 0 to know that
     * it is not initialized */
    uint32_t initialized = *(uint32_t*)addr;
    if (initialized == 0) {
        /* not yet initialized, so initialize values within superblock */
        init_superblock_structures(client);

        /* superblock structure has been initialized,
         * so set flag to indicate that fact */
        *(uint32_t*)addr = (uint32_t)0xDEADBEEF;
    } else {
        /* In this case, we have reattached to an existing superblock from
         * an earlier run.  We need to reset the segtree pointers to
         * newly allocated segtrees, because they point to structures
         * allocated in the last run whose memory addresses are no longer
         * valid. */

        /* TODO: what to do if a process calls unifyfs_init multiple times
         * in a run? */

        /* Clear any index entries from the cache.  We do this to ensure
         * the newly allocated seg trees are consistent with the extents
         * in the index. It would be nice to call unifyfs_sync_extents to flush
         * any entries to the server, but we can't do that since that will
         * try to rewrite the index using the trees, which point to invalid
         * memory at this point. */

        /* initialize count of key/value entries */
        *(client->state.write_index.ptr_num_entries) = 0;

        unifyfs_filemeta_t* meta;
        for (int i = 0; i < client->max_files; i++) {
            /* if the file entry is active, reset its segment trees */
            if (client->unifyfs_filelist[i].in_use) {
                /* got a live file, get pointer to its metadata */
                meta = unifyfs_get_meta_from_fid(client, i);
                assert(meta != NULL);

                /* Reset our segment tree that will record our writes */
                seg_tree_init(&meta->extents_sync);

                /* Reset our segment tree to track extents for all writes
                 * by this process, can be used to read back local data */
                if (client->use_local_extents ||
                    client->use_node_local_extents) {
                    seg_tree_init(&meta->extents);
                }
            }
        }
    }

    /* return starting memory address of super block */
    return UNIFYFS_SUCCESS;
}

int unifyfs_client_init(unifyfs_client* client)
{
    int rc;

    if (NULL == client) {
        return EINVAL;
    }

    /* add client to client_list */
    if (NULL == client_list) {
        client_list = arraylist_create(0);
        if (NULL == client_list) {
            LOGERR("failed to create client_list arraylist");
            return UNIFYFS_FAILURE;
        }
    }
    rc = arraylist_add(client_list, client);
    if (rc == -1) {
        LOGERR("failed to add client to client_list arraylist");
        return UNIFYFS_FAILURE;
    }

    if (!client->state.initialized) {

        // print log messages to stderr
        unifyfs_log_open(NULL);

        // initialize configuration (if not already done)
        unifyfs_cfg_t* client_cfg = &(client->cfg);
        if (client_cfg->ptype != UNIFYFS_CLIENT) {
            rc = unifyfs_config_init(client_cfg, 0, NULL, 0, NULL);
            if (rc) {
                LOGERR("failed to initialize configuration.");
                return UNIFYFS_FAILURE;
            }
            client_cfg->ptype = UNIFYFS_CLIENT;
        }

        // set log level from config
        char* cfgval = client_cfg->log_verbosity;
        if (cfgval != NULL) {
            long l;
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_set_log_level((unifyfs_log_level_t)l);
            }
        }

        /* determine the size of the superblock */
        size_t shm_super_size = get_superblock_size(client);

        /* get a superblock of shared memory and initialize our
         * global variables for this block */
        int rc = init_superblock_shm(client, shm_super_size);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to initialize superblock shmem");
            return rc;
        }

        /* initialize log-based I/O context */
        rc = unifyfs_logio_init_client(client->state.app_id,
                                       client->state.client_id,
                                       &(client->cfg),
                                       &(client->state.logio_ctx));
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to initialize log-based I/O (rc = %s)",
                   unifyfs_rc_enum_str(rc));
            return rc;
        }

        /* initialize client->active_mreads arraylist */
        client->active_mreads =
            arraylist_create(UNIFYFS_CLIENT_MAX_READ_COUNT);
        if (NULL == client->active_mreads) {
            LOGERR("failed to create arraylist for active reads");
            return UNIFYFS_FAILURE;
        }

        /* initialize client->active_mreads arraylist */
        client->active_transfers =
            arraylist_create(UNIFYFS_CLIENT_MAX_FILES);
        if (NULL == client->active_transfers) {
            LOGERR("failed to create arraylist for active transfers");
            return UNIFYFS_FAILURE;
        }

        pthread_mutexattr_t mux_recursive;
        pthread_mutexattr_init(&mux_recursive);
        pthread_mutexattr_settype(&mux_recursive, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&(client->sync), &mux_recursive);

        /* remember that we've now initialized the library */
        client->state.initialized = 1;
    }

    return UNIFYFS_SUCCESS;
}

/* free resources allocated during unifyfs_client_init().
 * generally, we do this in reverse order with respect to
 * how things were initialized */
int unifyfs_client_fini(unifyfs_client* client)
{
    int rc = UNIFYFS_SUCCESS;

    if (NULL == client) {
        return EINVAL;
    }

    int list_pos = -1;
    unifyfs_client* list_clnt = unifyfs_find_client(client->state.app_id,
                                                    client->state.client_id,
                                                    &list_pos);
    if (list_clnt != client) {
        LOGWARN("mismatch on client_list client");
    } else {
        /* remove from client_list */
        arraylist_remove(client_list, list_pos);
    }

    if (!client->state.initialized) {
        /* not initialized yet, so we shouldn't call finalize */
        return UNIFYFS_FAILURE;
    }

    pthread_mutex_lock(&(client->sync));

    if (NULL != client->active_mreads) {
        arraylist_free(client->active_mreads);
    }

    if (NULL != client->active_transfers) {
        arraylist_free(client->active_transfers);
    }

    pthread_mutex_unlock(&(client->sync));
    pthread_mutex_destroy(&(client->sync));

    /* close spillover files */
    if (NULL != client->state.logio_ctx) {
        unifyfs_logio_close(client->state.logio_ctx, 0);
        client->state.logio_ctx = NULL;
    }

    /* detach from superblock shmem, but don't unlink the file so that
     * a later client can reattach. */
    unifyfs_shm_free(&(client->state.shm_super_ctx));

    /* no longer initialized, so update the flag */
    client->state.initialized = 0;

    return rc;
}

/* find client in client_list with given app_id and client_id */
unifyfs_client* unifyfs_find_client(int app_id,
                                    int client_id,
                                    int* list_position)
{
    if (NULL == client_list) {
        return NULL;
    }

    int n_clients = arraylist_size(client_list);
    for (int i = 0; i < n_clients; i++) {
        void* item = arraylist_get(client_list, i);
        if (NULL != item) {
            unifyfs_client* clnt = (unifyfs_client*) item;
            if ((clnt->state.app_id == app_id) &&
                (clnt->state.client_id == client_id)) {
                if (NULL != list_position) {
                    *list_position = i;
                }
                return clnt;
            }
        }
    }
    return NULL;
}

/* Sync all the write extents for any client files to the server.
 * Returns UNIFYFS_SUCCESS on success, failure code otherwise */
int unifyfs_sync_files(unifyfs_client* client)
{
    int ret = UNIFYFS_SUCCESS;

    /* sync every active file */
    pthread_mutex_lock(&(client->sync));
    for (int i = 0; i < client->max_files; i++) {
        if (client->unifyfs_filelist[i].in_use) {
            /* got an active file, so sync this file id */
            int rc = unifyfs_fid_sync_extents(client, i);
            if (UNIFYFS_SUCCESS != rc) {
                ret = rc;
            }
        }
    }
    pthread_mutex_unlock(&(client->sync));

    return ret;
}
