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

#ifndef UNIFYFS_API_H
#define UNIFYFS_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// libunifyfs_common headers
#include "unifyfs_rc.h"
#include "unifyfs_configurator.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public Types
 */

/* UnifyFS file system client (opaque struct) */
struct unifyfs_client;

/* UnifyFS file system handle (opaque pointer) */
typedef struct unifyfs_client* unifyfs_handle;

/* invalid UnifyFS file system handle */
#define UNIFYFS_INVALID_HANDLE ((unifyfs_handle)NULL)


/* global file id type */
typedef uint32_t unifyfs_gfid;

/* a valid gfid generated via MD5 hash will never be zero */
#define UNIFYFS_INVALID_GFID ((unifyfs_gfid)0)

/* enumeration of supported I/O request operations */
typedef enum unifyfs_ioreq_op {
    UNIFYFS_IOREQ_NOP = 0,
    UNIFYFS_IOREQ_OP_READ,
    UNIFYFS_IOREQ_OP_WRITE,
    UNIFYFS_IOREQ_OP_SYNC_DATA,
    UNIFYFS_IOREQ_OP_SYNC_META,
    UNIFYFS_IOREQ_OP_TRUNC,
    UNIFYFS_IOREQ_OP_ZERO,
} unifyfs_ioreq_op;

/* enumeration of I/O request states */
typedef enum unifyfs_ioreq_state {
    UNIFYFS_IOREQ_STATE_INVALID = 0,
    UNIFYFS_IOREQ_STATE_IN_PROGRESS,
    UNIFYFS_IOREQ_STATE_CANCELED,
    UNIFYFS_IOREQ_STATE_COMPLETED
} unifyfs_ioreq_state;

/* structure to hold I/O request result values */
typedef struct unifyfs_ioreq_result {
    int error;
    int rc;
    size_t count;
} unifyfs_ioreq_result;

/* I/O request structure */
typedef struct unifyfs_io_request {
    /* user-specified fields */
    void* user_buf;
    size_t nbytes;
    off_t offset;
    unifyfs_gfid gfid;
    unifyfs_ioreq_op op;

    /* async callbacks (not yet supported)
     *
     * unifyfs_req_notify_fn fn;
     * void* notify_user_data;
     */

    /* status/result fields */
    unifyfs_ioreq_state state;
    unifyfs_ioreq_result result;

    /* internal fields */
    int _reqid;
} unifyfs_io_request;

/* enumeration of supported I/O request operations */
typedef enum unifyfs_transfer_mode {
    UNIFYFS_TRANSFER_MODE_INVALID = 0,
    UNIFYFS_TRANSFER_MODE_COPY, // simple copy to destination
    UNIFYFS_TRANSFER_MODE_MOVE  // copy, then remove source
} unifyfs_transfer_mode;

/* File transfer request structure */
typedef struct unifyfs_transfer_request {
    /* user-specified fields */
    const char* src_path;
    const char* dst_path;
    unifyfs_transfer_mode mode;
    int use_parallel;

    /* async callbacks (not yet supported)
     *
     * unifyfs_req_notify_fn fn;
     * void* notify_user_data;
     */

    /* status/result fields */
    unifyfs_ioreq_state state;
    unifyfs_ioreq_result result;

    /* internal fields */
    int _reqid;
} unifyfs_transfer_request;

/* Global file status struct */
typedef struct unifyfs_status {
    int laminated;
    int mode;
    off_t local_file_size;
    off_t global_file_size;
    size_t local_write_nbytes;
} unifyfs_status;


/*
 * Public Methods
 */

/*
 * Initialize client's use of UnifyFS with given mountpoint
 * and configuration. Sets file system handle on success.
 *
 * @param[in]   mountpoint  Requested mount prefix
 * @param[in]   options     Array of configuration options
 * @param[in]   n_opts      Size of options array
 * @param[out]  fshdl       Client file system handle
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_initialize(const char* mountpoint,
                              unifyfs_cfg_option* options, int n_opts,
                              unifyfs_handle* fshdl);

/*
 * Finalize client's use of UnifyFS. Invalidates given file
 * system handle.
 *
 * @param[in]   fshdl       Client file system handle
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_finalize(unifyfs_handle fshdl);

/*
 * Create and open a new file in UnifyFS.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   flags       File creation flags
 * @param[in]   filepath    Path of file to create
 * @param[out]  gfid        Global file id of created file
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_create(unifyfs_handle fshdl,
                          const int flags,
                          const char* filepath,
                          unifyfs_gfid* gfid);

/*
 * Open an existing file in UnifyFS.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   flags       File access flags
 * @param[in]   filepath    Path of file to open
 * @param[out]  gfid        Global file id of opened file
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_open(unifyfs_handle fshdl,
                        const int flags,
                        const char* filepath,
                        unifyfs_gfid* gfid);

/*
 * Get global file status.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   gfid        Global file id of target file
 * @param[out]  st          File status structure
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_stat(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid,
                        unifyfs_status* st);

/*
 * Synchronize client writes with global metadata. After successful
 * completion, writes will be visible to other clients.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   gfid        Global file id of target file
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_sync(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid);

/*
 * Laminate the given file. After successful completion, writes and other
 * file state modifying operations will not be permitted by any client.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   filepath    Path of file to laminate
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_laminate(unifyfs_handle fshdl,
                            const char* filepath);

/*
 * Remove an existing file from UnifyFS.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   filepath    Path of file to remove
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_remove(unifyfs_handle fshdl,
                          const char* filepath);

/*
 * Dispatch a set of I/O requests to UnifyFS.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of I/O requests array
 * @param[in]   reqs        Array of I/O requests
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_dispatch_io(unifyfs_handle fshdl,
                               const size_t nreqs,
                               unifyfs_io_request* reqs);

/*
 * Cancel a set of outstanding I/O requests. Only requests that
 * are still in-progress will be canceled.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of I/O requests array
 * @param[in]   reqs        Array of I/O requests
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_cancel_io(unifyfs_handle fshdl,
                             const size_t nreqs,
                             unifyfs_io_request* reqs);

/*
 * Wait for a set of I/O requests to be completed or canceled. When
 * a non-zero value is passed for 'waitall', the function will return
 * only after all I/O requests in the array have completed. When zero
 * is passed for 'waitall', the function will return after as soon as
 * any individual request has completed.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of I/O requests array
 * @param[in]   reqs        Array of I/O requests
 * @param[in]   waitall     Wait-all behavior flag
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_wait_io(unifyfs_handle fshdl,
                           const size_t nreqs,
                           unifyfs_io_request* reqs,
                           const int waitall);

/*
 * Dispatch a set of transfer requests to UnifyFS.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of transfer requests array
 * @param[in]   reqs        Array of transfer requests
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_dispatch_transfer(unifyfs_handle fshdl,
                                     const size_t nreqs,
                                     unifyfs_transfer_request* reqs);

/*
 * Cancel a set of outstanding transfer requests. Only transfers that
 * are still in-progress will be canceled.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of transfer requests array
 * @param[in]   reqs        Array of transfer requests
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_cancel_transfer(unifyfs_handle fshdl,
                                   const size_t nreqs,
                                   unifyfs_transfer_request* reqs);

/*
 * Wait for a set of transfer requests to be completed or canceled. When
 * a non-zero value is passed for waitall, the function will return
 * only after all transfer requests in the array have completed. When zero
 * is passed for waitall, the function will return after as soon as
 * any individual request has completed.
 *
 * @param[in]   fshdl       Client file system handle
 * @param[in]   nreqs       Size of transfer requests array
 * @param[in]   reqs        Array of transfer requests
 * @param[in]   waitall     Wait-all behavior flag
 *
 * @return      UnifyFS success or failure code
 */
unifyfs_rc unifyfs_wait_transfer(unifyfs_handle fshdl,
                                 const size_t nreqs,
                                 unifyfs_transfer_request* reqs,
                                 const int waitall);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_API_H
