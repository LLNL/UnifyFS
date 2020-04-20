#ifndef __UNIFYFS_GROUP_RPC_H
#define __UNIFYFS_GROUP_RPC_H

#include "unifyfs_tree.h"

int unifyfs_broadcast_extent_tree(int gfid);

/**
 * @brief
 *
 * @param gfid
 *
 * @return
 */
static inline int unifyfs_invoke_broadcast_extents_rpc(int gfid)
{
    return unifyfs_broadcast_extent_tree(gfid);
}

/**
 * @brief
 *
 * @param gfid
 * @param filesize
 *
 * @return
 */
int unifyfs_invoke_filesize_rpc(int gfid, size_t* filesize);

/**
 * @brief
 *
 * @param gfid
 * @param filesize
 *
 * @return
 */
int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize);

/**
 * @brief
 *
 * @param gfid
 * @param create
 * @param attr
 *
 * @return
 */
int unifyfs_invoke_metaset_rpc(int gfid, int create,
                               unifyfs_file_attr_t* attr);

/**
 * @brief
 *
 * @param gfid
 *
 * @return
 */
int unifyfs_invoke_unlink_rpc(int gfid);

#endif /* __UNIFYFS_GROUP_RPC_H */
