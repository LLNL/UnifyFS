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

#ifndef UNIFYFS_FID_H
#define UNIFYFS_FID_H

#include "unifyfs_api_internal.h"


/* ---  file id (fid) management --- */

/* Allocate a fid slot for a new entry.
 * Return the fid or -1 on error */
int unifyfs_fid_alloc(unifyfs_client* client);

/* Add the fid slot back to the free pool */
int unifyfs_fid_free(unifyfs_client* client,
                     int fid);


/* --- fid metadata updates --- */

/* Add a new file and initialize metadata.
 * Returns the new fid, or negative error value */
int unifyfs_fid_create_file(unifyfs_client* client,
                            const char* path,
                            int private);

/* Add a new directory and initialize metadata.
 * Returns the new fid, or a negative error value */
int unifyfs_fid_create_directory(unifyfs_client* client,
                                 const char* path);

/* Opens a file with specified path, access flags, and permissions.
 * Sets *outfid to file id and *outpos to current file position.
 */
int unifyfs_fid_open(unifyfs_client* client,
                     const char* path,
                     int flags,
                     mode_t mode,
                     int* outfid,
                     off_t* outpos);

/* Close file with given file id */
int unifyfs_fid_close(unifyfs_client* client,
                      int fid);

/* Update local metadata for file from global metadata */
int unifyfs_fid_update_file_meta(unifyfs_client* client,
                                 int fid,
                                 unifyfs_file_attr_t* gfattr);

/* Unlink file and then delete its associated state */
int unifyfs_fid_unlink(unifyfs_client* client,
                       int fid);

/* Release the file's metadata and storage resources, and return
 * the fid to free stack */
int unifyfs_fid_delete(unifyfs_client* client,
                       int fid);

/* Use local file metadata to update global metadata */
int unifyfs_set_global_file_meta_from_fid(unifyfs_client* client,
                                          int fid,
                                          unifyfs_file_attr_op_e op);


/* --- fid metadata queries --- */

/* Given a file id, return a pointer to the metadata,
 * otherwise return NULL */
unifyfs_filemeta_t* unifyfs_get_meta_from_fid(unifyfs_client* client,
                                              int fid);

/* Return 1 if fid is laminated, 0 if not */
int unifyfs_fid_is_laminated(unifyfs_client* client,
                             int fid);

/* Given a fid, return the path */
const char* unifyfs_path_from_fid(unifyfs_client* client,
                                  int fid);

/* Given a path, return the fid */
int unifyfs_fid_from_path(unifyfs_client* client,
                          const char* path);

/* Given a fid, return a gfid */
int unifyfs_gfid_from_fid(unifyfs_client* client,
                          int fid);

/* Returns fid for corresponding gfid, if one is active.
 * Otherwise, returns -1 */
int unifyfs_fid_from_gfid(unifyfs_client* client,
                          int gfid);

/* Checks to see if fid is a directory.
 * Returns 1 for yes, 0 for no */
int unifyfs_fid_is_dir(unifyfs_client* client,
                       int fid);

/* Checks to see if a directory is empty.
 * Assumes that check for is_dir has already been made.
 * Only checks for full path matches, does not check relative paths
 * (i.e., '../dirname' will not work).
 * Returns 1 for yes it is empty, 0 for no */
int unifyfs_fid_is_dir_empty(unifyfs_client* client,
                             const char* path);

/* Return current global size of given file id */
off_t unifyfs_fid_global_size(unifyfs_client* client,
                              int fid);

/* Return current size of given file id. If the file is laminated,
 * returns the global size.  Otherwise, returns the local size. */
off_t unifyfs_fid_logical_size(unifyfs_client* client,
                               int fid);


/* --- fid I/O operations --- */

/* Write count bytes from buf into file starting at offset pos */
int unifyfs_fid_write(
    unifyfs_client* client,
    int fid,         /* local file id to write to */
    off_t pos,       /* starting offset within file */
    const void* buf, /* buffer of data to be written */
    size_t count,    /* number of bytes to write */
    size_t* nwritten /* returns number of bytes written */
);

/* Truncate file to given length. Removes or truncates file extents
 * in metadata that are past the given length. */
int unifyfs_fid_truncate(unifyfs_client* client,
                         int fid,
                         off_t length);

/* Sync extent data for file to storage */
int unifyfs_fid_sync_data(unifyfs_client* client,
                          int fid);

/* Sync extent metadata for file to server if needed */
int unifyfs_fid_sync_extents(unifyfs_client* client,
                             int fid);



#endif /* UNIFYFS_FID_H */
