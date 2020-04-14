#ifndef __UNIFYFS_COLLECTIVES_H
#define __UNIFYFS_COLLECTIVES_H

#include "unifyfs_tree.h"

int unifyfs_broadcast_extent_tree(int gfid);

int unifyfs_invoke_filesize_rpc(int gfid, size_t *filesize);

int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize);

int unifyfs_invoke_metaset_rpc(int gfid, int create,
                                unifyfs_file_attr_t *attr);

int unifyfs_invoke_unlink_rpc(int gfid);

#endif /* __UNIFYFS_COLLECTIVES_H */
