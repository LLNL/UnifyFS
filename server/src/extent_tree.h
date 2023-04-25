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

#ifndef __EXTENT_TREE_H__
#define __EXTENT_TREE_H__

#include "unifyfs_global.h"

typedef struct extent_metadata {
    /* extent metadata */
    unsigned long start;    /* logical offset of extent's first byte */
    unsigned long end;      /* logical offset of extent's last byte */

    /* logio metadata */
    unsigned long log_pos;  /* physical offset of data in log */
    int svr_rank;           /* rank of server hosting the log */
    int app_id;             /* application id (namespace) of client */
    int cli_id;             /* rank (on host server) of client */
} extent_metadata;

#define extent_length(meta_ptr) \
    ((size_t)1 + ((meta_ptr)->end - (meta_ptr)->start))

#define extent_offset(meta_ptr) \
    (off_t)((meta_ptr)->start)

struct extent_tree_node {
    RB_ENTRY(extent_tree_node) entry;
    struct extent_metadata extent;
};

struct extent_tree {
    RB_HEAD(ext_tree, extent_tree_node) head;
    ABT_rwlock rwlock;
    unsigned long count;     /* number of segments stored in tree */
    unsigned long max;       /* maximum logical offset value in the tree */
};

/* Returns 0 on success, positive non-zero error code otherwise */
int extent_tree_init(struct extent_tree* tree);

/*
 * Remove all nodes in tree, but keep it initialized so you can
 * extent_tree_add() to it.
 */
void extent_tree_clear(struct extent_tree* tree);

/*
 * Remove and free all nodes in the tree.
 */
void extent_tree_destroy(struct extent_tree* tree);

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int extent_tree_add(struct extent_tree* tree,
                    struct extent_metadata* extent);

/* search tree for entry that overlaps with given start/end
 * offsets, return first overlapping entry if found, NULL otherwise,
 * assumes caller has lock on tree */
struct extent_tree_node* extent_tree_find(
    struct extent_tree* tree, /* tree to search */
    unsigned long start,      /* starting offset to search */
    unsigned long end         /* ending offset to search */
);

/* truncate extents to use new maximum, discards extent entries
 * that exceed the new truncated size, and rewrites any entry
 * that overlaps */
int extent_tree_truncate(
    struct extent_tree* tree, /* tree to truncate */
    unsigned long size        /* size to truncate extents to */
);

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    extent_tree_rdlock(tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    extent_tree_unlock(tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the tree before doing the iteration (see
 * extent_tree_rdlock()/extent_tree_wrlock()/extent_tree_unlock()).
 */
struct extent_tree_node* extent_tree_iter(
    struct extent_tree* tree,
    struct extent_tree_node* start);

/* Return the number of segments in the segment tree */
unsigned long extent_tree_count(struct extent_tree* tree);

/* Return the maximum ending logical offset in the tree */
unsigned long extent_tree_max_offset(struct extent_tree* tree);

/*
 * Locking functions for use with extent_tree_iter().  They allow you to
 * lock the tree to iterate over it:
 *
 *    extent_tree_rdlock(&tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    extent_tree_unlock(&tree);
 */

/*
 * Lock a extent_tree for reading.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_rdlock(struct extent_tree* tree);

/*
 * Lock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_wrlock(struct extent_tree* tree);

/*
 * Unlock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_unlock(struct extent_tree* tree);

int extent_tree_get_chunk_list(
    struct extent_tree* tree,  /* extent tree to search */
    unsigned long offset,      /* starting logical offset */
    unsigned long len,         /* length of extent */
    unsigned int* n_chunks,    /* [out] number of chunks returned */
    chunk_read_req_t** chunks, /* [out] chunk array */
    int* extent_covered);      /* [out] set=1 if extent fully covered */

/* dump method for debugging extent trees */
static inline
void extent_tree_dump(struct extent_tree* tree)
{
    if (NULL == tree) {
        return;
    }

    extent_tree_rdlock(tree);

    struct extent_tree_node* node = NULL;
    while ((node = extent_tree_iter(tree, node))) {
        LOGDBG("[%lu-%lu] @ %d(%d:%d) log offset %lu",
               node->extent.start, node->extent.end, node->extent.svr_rank,
               node->extent.app_id, node->extent.cli_id, node->extent.log_pos);
    }

    extent_tree_unlock(tree);
}

#endif /* __EXTENT_TREE_H__ */
