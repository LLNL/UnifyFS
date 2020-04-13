#ifndef __UNIFYFS_INODE_H
#define __UNIFYFS_INODE_H

#include <pthread.h>
#include "tree.h"
#include "extent_tree.h"
#include "unifyfs_meta.h"
#include "unifyfs_global.h"

/**
 * @brief file and directory inode structure. this holds:
 */
struct unifyfs_inode {
    RB_ENTRY(unifyfs_inode) inode_tree_entry; /** tree entry for global inode
                                                tree */

    int gfid;                     /* global file identifier */
    unifyfs_file_attr_t attr;     /* file attributes */
    pthread_rwlock_t rwlock;      /* rwlock for accessing this structure */

    struct extent_tree* local_extents;  /* tree for local data segments */
    struct extent_tree* remote_extents; /* tree for remote data segments */
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
int unifyfs_inode_update_attr(int gfid, unifyfs_file_attr_t* attr);

/**
 * @brief create a new or update an existing inode.
 *
 * @param gfid global file identifier
 * @param create try to create a new inode if set
 * @param attr file attributes
 *
 * @return 0 on success, errno otherwise
 */
static inline
int unifyfs_inode_metaset(int gfid, int create, unifyfs_file_attr_t* attr)
{
    int ret = 0;

    if (create) {
        ret = unifyfs_inode_create(gfid, attr);
    } else {
        ret = unifyfs_inode_update_attr(gfid, attr);
    }

    return ret;
}

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
 * @brief
 *
 * @param gfid
 * @param num_extents
 * @param nodes
 *
 * @return
 */
int unifyfs_inode_add_local_extents(int gfid, int num_extents,
                                    struct extent_tree_node* nodes);

/**
 * @brief get the maximum file size from the local extent tree of given file
 *
 * @param gfid global file identifier
 * @param offset [out] file offset to be filled by this function
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_get_extent_size(int gfid, size_t* offset);

/**
 * @brief
 *
 * @param gfid
 * @param n
 * @param nodes
 *
 * @return
 */
int unifyfs_inode_get_local_extents(int gfid, size_t *n,
                                    struct extent_tree_node **nodes);

/**
 * @brief
 *
 * @param gfid
 * @param n
 * @param nodes
 *
 * @return
 */
int unifyfs_inode_add_remote_extents(int gfid, int n,
                                     struct extent_tree_node *nodes);

int unifyfs_inode_span_extents(
    int gfid,                        /* global file id we're looking in */
    unsigned long start,             /* starting logical offset */
    unsigned long end,               /* ending logical offset */
    int max,                         /* maximum number of key/vals to return */
    void* keys,             /* array of length max for output keys */
    void* vals,             /* array of length max for output values */
    int* outnum);                    /* number of entries returned */

/*
 * for debugging
 */
int unifyfs_inode_dump(int gfid);

#endif /* __UNIFYFS_INODE_H */

