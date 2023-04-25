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

#ifndef __UNIFYFS_INODE_H
#define __UNIFYFS_INODE_H

#include "unifyfs_global.h"
#include "extent_tree.h"

typedef struct pending_extents_item {
    client_rpc_req_t* client_req; /* req details, including response handle */
    unsigned int num_extents;     /* number of extents in array */
    extent_metadata* extents;     /* array of extent metadata */
} pending_extents_item;

/**
 * @brief file and directory inode structure. this holds:
 */
struct unifyfs_inode {
    /* tree entry for global inode tree */
    RB_ENTRY(unifyfs_inode) inode_tree_entry;

    int gfid;                     /* global file identifier */
    unifyfs_file_attr_t attr;     /* file attributes */
    struct extent_tree* extents;  /* extent information */
    arraylist_t* pending_extents; /* list of pending_extents_item */

    ABT_rwlock rwlock;            /* reader-writer lock */
};

/**
 * @brief create a new inode with given parameters. The newly created inode
 * will be inserted to the global inode tree (global_inode_tree).
 *
 * @param gfid global file identifier.
 * @param attr attributes of the new file.
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_create(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief delete an inode and all its contents
 *
 * @param ino  inode to destroy
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_destroy(struct unifyfs_inode* ino);

/**
 * @brief update the attributes of file with @gfid. The attributes are
 * selectively updated with unifyfs_file_attr_update() function (see
 * common/unifyfs_meta.h).
 *
 * @param gfid global file identifier
 * @param attr new attributes
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_update_attr(int gfid, int attr_op,
                              unifyfs_file_attr_t* attr);

/**
 * @brief create a new or update an existing inode.
 *
 * @param gfid global file identifier
 * @param create try to create a new inode if set
 * @param attr file attributes
 *
 * @return 0 on success, errno otherwise
 */

int unifyfs_inode_metaset(int gfid, int attr_op,
                          unifyfs_file_attr_t* attr);

/**
 * @brief read attributes for file with @gfid.
 *
 * @param      gfid  global file identifier
 *
 * @param[out] attr  output file attributes
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_metaget(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief unlink file with @gfid. this will remove the target file inode from
 * the global inode tree.
 *
 * @param gfid  global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_unlink(int gfid);

/**
 * @brief truncate size of file with @gfid to @size.
 *
 * @param gfid  global file identifier
 * @param size  new file size
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_truncate(int gfid, unsigned long size);

/**
 * @brief get the local extent array from the target inode
 *
 * @param gfid     the global file identifier
 * @param n        pointer to size of the extents array
 * @param extents  pointer to extents array (caller should free)
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_extents(int gfid,
                              size_t* n,
                              extent_metadata** extents);

/**
 * @brief add extents pending sync to the inode
 *
 * @param gfid               the global file identifier
 * @param client_req         the client req to sync the extents
 * @param num_extents        the number of extents in @extents
 * @param extents            an array of extents to be added as pending
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_add_pending_extents(int gfid,
                                      client_rpc_req_t* client_req,
                                      int num_extents,
                                      extent_metadata* extents);

/**
 * @brief check if inode has pending extents to sync
 *
 * @param   gfid    the global file identifier
 *
 * @return true if pending extents exist, false otherwise
 */
bool unifyfs_inode_has_pending_extents(int gfid);

/**
 * @brief retrieve pending extents list for the inode
 *        (future adds will go to a new pending list)
 *
 * @param       gfid            the global file identifier
 * @param[out]  pending_list    the list of pending extents (if any)
 *
 * @return 0 on success, errno otherwise
 */

int unifyfs_inode_get_pending_extents(int gfid,
                                      arraylist_t** pending_list);

/**
 * @brief add new extents to the inode
 *
 * @param gfid          the global file identifier
 * @param num_extents   the number of extents in @extents
 * @param extents       an array of extents to be added
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_add_extents(int gfid,
                              int num_extents,
                              extent_metadata* extents);

/**
 * @brief get the maximum file size from the local extent tree of given file
 *
 * @param      gfid     global file identifier
 *
 * @param[out] outsize  output file size
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_filesize(int gfid, size_t* outsize);

/**
 * @brief set the given file as laminated
 *
 * @param gfid global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_laminate(int gfid);

/**
 * @brief Get chunks for given file extent
 *
 * @param extent              target file extent
 *
 * @param[out] n_chunks       number of output chunk locations
 * @param[out] chunks         array of output chunk locations
 * @param[out] full_coverage  set to 1 if chunks fully cover extent
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_inode_get_extent_chunks(unifyfs_extent_t* extent,
                                    unsigned int* n_chunks,
                                    chunk_read_req_t** chunks,
                                    int* full_coverage);

/**
 * @brief Get chunk locations for an array of file extents
 *
 * @param n_extents  number of input extents
 * @param extents    array or requested extents
 *
 * @param[out] n_locs         number of output chunk locations
 * @param[out] chunklocs      array of output chunk locations
 * @param[out] full_coverage  set to 1 if chunks fully cover extents
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_inode_resolve_extent_chunks(unsigned int n_extents,
                                        unifyfs_extent_t* extents,
                                        unsigned int* n_locs,
                                        chunk_read_req_t** chunklocs,
                                        int* full_coverage);

/**
 * @brief prints the inode information to the log stream
 *
 * @param gfid global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_dump(int gfid);


/**
 * @brief walks the tree and gets a list of the gfids for all the inodes
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_get_gfids(int* num_gfids, int** gfid_list);


/**
 * @brief Walk the tree and return a list file_attr_t structs for all files
 * that we own.
 *
 * Upon success, the caller will be responsible for freeing attr_list.  If
 * this function returns an error code, then the caller must *NOT* free
 * attr_list.
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_get_owned_files(unsigned int* num_files,
                            unifyfs_file_attr_t** attr_list);

#endif /* __UNIFYFS_INODE_H */

