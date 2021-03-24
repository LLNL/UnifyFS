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

/**
 * @brief file extent descriptor
 */
struct unifyfs_inode_extent {
    int gfid;
    unsigned long offset;
    unsigned long length;
};
typedef struct unifyfs_inode_extent unifyfs_inode_extent_t;

/**
 * @brief file and directory inode structure. this holds:
 */
struct unifyfs_inode {
    /* tree entry for global inode tree */
    RB_ENTRY(unifyfs_inode) inode_tree_entry;

    int gfid;                     /* global file identifier */
    unifyfs_file_attr_t attr;     /* file attributes */
    struct extent_tree* extents;  /* extent information */

    pthread_rwlock_t rwlock;      /* rwlock for pthread access */
    ABT_mutex abt_sync;           /* mutex for argobots ULT access */
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
 * @param gfid global file identifier
 * @param attr [out] file attributes to be filled
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_metaget(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief unlink file with @gfid. this will remove the target file inode from
 * the global inode tree.
 *
 * @param gfid global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_unlink(int gfid);

/**
 * @brief truncate size of file with @gfid to @size.
 *
 * @param gfid global file identifier
 * @param size new file size
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_truncate(int gfid, unsigned long size);

/**
 * @brief get the local extent array from the target inode
 *
 * @param gfid the global file identifier
 * @param n the number of extents, set by this function
 * @param nodes the pointer to the array of extents, caller should free this
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_extents(int gfid, size_t* n,
                              struct extent_tree_node** nodes);

/**
 * @brief add new extents to the inode
 *
 * @param gfid the global file identifier
 * @param n the number of new extents in @nodes
 * @param nodes an array of extents to be added
 *
 * @return
 */
int unifyfs_inode_add_extents(int gfid, int n, struct extent_tree_node* nodes);

/**
 * @brief get the maximum file size from the local extent tree of given file
 *
 * @param gfid global file identifier
 * @param offset [out] file offset to be filled by this function
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_filesize(int gfid, size_t* offset);

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
 * @param extent  target file extent
 *
 * @param[out] n_chunks  number of output chunk locations
 * @param[out] chunks    array of output chunk locations
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_inode_get_extent_chunks(unifyfs_inode_extent_t* extent,
                                    unsigned int* n_chunks,
                                    chunk_read_req_t** chunks);

/**
 * @brief Get chunk locations for an array of file extents
 *
 * @param n_extents  number of input extents
 * @param extents    array or requested extents
 *
 * @param[out] n_locs     number of output chunk locations
 * @param[out] chunklocs  array of output chunk locations
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_inode_resolve_extent_chunks(unsigned int n_extents,
                                        unifyfs_inode_extent_t* extents,
                                        unsigned int* n_locs,
                                        chunk_read_req_t** chunklocs);

/**
 * @brief calls extents_tree_span, which will do:
 *
 * given an extent tree and starting and ending logical offsets, fill in
 * key/value entries that overlap that range, returns at most max entries
 * starting from lowest starting offset, sets outnum with actual number of
 * entries returned
 *
 * @param gfid global file id
 * @param start starting logical offset
 * @param end ending logical offset
 * @param max maximum number of key/vals to return
 * @param keys array of length max for output keys
 * @param vals array of length max for output values
 * @param outnum number of entries returned
 *
 * @return
 */
int unifyfs_inode_span_extents(
    int gfid,               /* global file id we're looking in */
    unsigned long start,    /* starting logical offset */
    unsigned long end,      /* ending logical offset */
    int max,                /* maximum number of key/vals to return */
    void* keys,             /* array of length max for output keys */
    void* vals,             /* array of length max for output values */
    int* outnum);           /* number of entries returned */

/**
 * @brief prints the inode information to the log stream
 *
 * @param gfid global file identifier
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_dump(int gfid);

#endif /* __UNIFYFS_INODE_H */

