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

#ifndef _UNIFYFS_CLIENT_READ_H
#define _UNIFYFS_CLIENT_READ_H

#include "unifyfs-internal.h"
#include "unifyfs_api_internal.h"
#include "unifyfs_fid.h"

// headers for client-server RPCs
#include "unifyfs_client_rpcs.h"
#include "margo_client.h"

/* This structure defines a client read request for one file corresponding to
 * the global file id (gfid). It describes a contiguous read extent starting
 * at offset with given length. */
typedef struct {
    /* The read request parameters */
    int gfid;             /* global id of file to be read */
    size_t offset;        /* logical file offset */
    size_t length;        /* requested number of bytes */
    char* buf;            /* user buffer to place data */
    struct aiocb* aiocbp; /* user aiocb* from aio or listio */

    /* These two variables define the byte offset range of the extent for
     * which we filled valid data.
     * If cover_begin_offset != 0, there is a gap at the beginning
     * of the read extent that should be zero-filled.
     * If cover_end_offset != (length - 1), it was a short read. */
    volatile size_t cover_begin_offset;
    volatile size_t cover_end_offset;

    /* nread is the user-visible number of bytes read. Since this includes
     * any gaps, nread should be set to (cover_end_offset + 1) when the
     * read request has been fully serviced. */
    size_t nread;

    /* errcode holds any error code encountered during the read.
     * The error may be an internal error value (unifyfs_rc_e) or a
     * normal POSIX error code. It will be converted to a valid errno value
     * for use in returning from the syscall. */
    int errcode;
} read_req_t;

/* Structure used by the client to track completion state for a
 * set of read requests submitted by a single client syscall.
 * The server will return data for each read request in separate
 * rpc calls. */
typedef struct {
    unifyfs_client* client;

    unsigned int id;         /* unique id for this set of read requests */
    unsigned int n_reads;    /* number of read requests */
    read_req_t* reqs;        /* array of read requests */

    /* the following is for synchronizing access/updates to below state */
    ABT_mutex sync;
    volatile unsigned int n_complete; /* number of completed requests */
    volatile unsigned int n_error;    /* number of requests that had errors */
} client_mread_status;

/* Create a new mread request containing the n_reads requests provided
 * in read_reqs array */
client_mread_status* client_create_mread_request(unifyfs_client* client,
                                                 int n_reads,
                                                 read_req_t* read_reqs);

/* Remove the mread status */
int client_remove_mread_request(client_mread_status* mread);

/* Retrieve the mread request corresponding to the given request_id */
client_mread_status* client_get_mread_status(unifyfs_client* client,
                                             unsigned int request_id);

/* Update the mread status for the request at the given req_index.
 * If the request is now complete, update the request's completion state
 * (i.e., errcode and nread) */
int client_update_mread_request(client_mread_status* mread,
                                unsigned int req_index,
                                int req_complete,
                                int req_error);


/* For the given read request and extent (file offset, length), calculate
 * the coverage including offsets from the beginning of the request and extent
 * and the coverage length. Return a pointer to the segment within the request
 * buffer where read data should be placed. */
char* get_extent_coverage(read_req_t* req,
                          size_t extent_file_offset,
                          size_t extent_length,
                          size_t* out_req_offset,
                          size_t* out_ext_offset,
                          size_t* out_length);

void update_read_req_coverage(read_req_t* req,
                              size_t extent_byte_offset,
                              size_t extent_length);

/* process a set of client read requests */
int process_gfid_reads(unifyfs_client* client,
                       read_req_t* in_reqs,
                       size_t in_count);

#endif // UNIFYFS_CLIENT_READ_H
