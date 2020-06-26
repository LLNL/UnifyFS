#ifndef __UNIFYFS_GROUP_RPC_H
#define __UNIFYFS_GROUP_RPC_H

#include "unifyfs_tree.h"
#include "unifyfs_inode.h"

/**
 * @brief
 *
 * @param gfid     target file
 * @param len      length of file extents array
 * @param extents  array of extents to broadcast
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_extents_rpc(int gfid,
                                         unsigned int len,
                                         struct extent_tree_node* extents);

/**
 * @brief
 *
 * @param gfid      target file
 * @param filesize  [out] file size
 *
 * @return success|failure
 */
int unifyfs_invoke_filesize_rpc(int gfid, size_t* filesize);

/**
 * @brief
 *
 * @param gfid      target file
 * @param filesize  requested new file size
 *
 * @return success|failure
 */
int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize);

/**
 * @brief
 *
 * @param gfid    target file
 * @param create  flag indicating if this is a newly created file
 * @param attr    file attributes to update
 *
 * @return success|failure
 */
int unifyfs_invoke_metaset_rpc(int gfid, int create,
                               unifyfs_file_attr_t* attr);

/**
 * @brief
 *
 * @param gfid  target file
 *
 * @return success|failure
 */
int unifyfs_invoke_unlink_rpc(int gfid);

/**
 * @brief
 *
 * @param gfid  target file
 *
 * @return success|failure
 */
int unifyfs_invoke_laminate_rpc(int gfid);

#endif /* __UNIFYFS_GROUP_RPC_H */
