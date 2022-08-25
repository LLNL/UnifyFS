/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "unifyfs-internal.h"
#include "unifyfs_api_internal.h"
#include "margo_client.h"

typedef struct transfer_request_status {
    unifyfs_client* client;
    unifyfs_transfer_request* req;

    /* set to 1 if source file is in client namespace, otherwise
     * destination file is in the namespace */
    int src_in_unify;

    /* the following is for synchronizing access/updates to below state */
    ABT_mutex sync;
    volatile unsigned int complete; /* has request completed? */
} client_transfer_status;


/* Create a new transfer status for the given transfer request and
 * insert it into client->active_transfers arraylist */
int client_create_transfer(unifyfs_client* client,
                           unifyfs_transfer_request* req,
                           bool src_in_unify);

/* Retrieve the transfer status for request with the given id */
client_transfer_status* client_get_transfer(unifyfs_client* client,
                                            unsigned int transfer_id);

/* Check if the transfer has completed */
bool client_check_transfer_complete(client_transfer_status* transfer);

/* Remove the transfer status from client->active_transfers arraylist */
int client_cleanup_transfer(unifyfs_client* client,
                           client_transfer_status* transfer);

/* Update the transfer status for the client (app_id + client_id)
 * transfer request (transfer_id) using the given error_code, transfer
 * size, and transfer time */
int client_complete_transfer(unifyfs_client* client,
                             int transfer_id,
                             int error_code,
                             size_t transfer_size_bytes,
                             double transfer_time_seconds);

/* Given an array of transfer requests, submit each request after
 * creating a transfer status structure for the request and storing it
 * within client->active_transfers */
int client_submit_transfers(unifyfs_client* client,
                            unifyfs_transfer_request* t_reqs,
                            size_t n_reqs);
