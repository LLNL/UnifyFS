#ifndef __EXTENT_TREE_H__
#define __EXTENT_TREE_H__
#include <pthread.h>
#include "tree.h"

struct extent_tree_node {
    RB_ENTRY(extent_tree_node) entry;
    unsigned long start; /* starting logical offset of range */
    unsigned long end;   /* ending logical offset of range */
    int svr_rank;        /* rank of server hosting data */
    int app_id;          /* application id (namespace) on server rank */
    int cli_id;          /* client rank on server rank */
    unsigned long pos;   /* physical offset of data in log */
};

struct extent_tree {
    RB_HEAD(inttree, extent_tree_node) head;
    pthread_rwlock_t rwlock;
    unsigned long count;     /* number of segments stored in tree */
    long max;                /* maximum logical offset value in the tree */
};

/* Returns 0 on success, positive non-zero error code otherwise */
int extent_tree_init(struct extent_tree* extent_tree);

/*
 * Remove all nodes in extent_tree, but keep it initialized so you can
 * extent_tree_add() to it.
 */
void extent_tree_clear(struct extent_tree* extent_tree);

/*
 * Remove and free all nodes in the extent_tree.
 */
void extent_tree_destroy(struct extent_tree* extent_tree);

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int extent_tree_add(
    struct extent_tree* extent_tree, /* tree to add new extent item */
    unsigned long start, /* logical starting offset of extent */
    unsigned long end,   /* logical ending offset of extent */
    int svr_rank,        /* rank of server hosting data */
    int app_id,          /* application id (namespace) on server rank */
    int cli_id,          /* client rank on server rank */
    unsigned long pos    /* physical offset of data in log */
);

/* search tree for entry that overlaps with given start/end
 * offsets, return first overlapping entry if found, NULL otherwise,
 * assumes caller has lock on tree */
struct extent_tree_node* extent_tree_find(
    struct extent_tree* extent_tree, /* tree to search */
    unsigned long start, /* starting offset to search */
    unsigned long end    /* ending offset to search */
);

/* truncate extents to use new maximum, discards extent entries
 * that exceed the new truncated size, and rewrites any entry
 * that overlaps */
int extent_tree_truncate(
    struct extent_tree* extent_tree, /* tree to truncate */
    unsigned long size               /* size to truncate extents to */
);

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    extent_tree_rdlock(extent_tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(extent_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    extent_tree_unlock(extent_tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the extent_tree before doing the iteration (see
 * extent_tree_rdlock()/extent_tree_wrlock()/extent_tree_unlock()).
 */
struct extent_tree_node* extent_tree_iter(
    struct extent_tree* extent_tree,
    struct extent_tree_node* start);

/* Return the number of segments in the segment tree */
unsigned long extent_tree_count(struct extent_tree* extent_tree);

/* Return the maximum ending logical offset in the tree */
unsigned long extent_tree_max(struct extent_tree* extent_tree);

/* Returns the size of the local extents (local file size) */
unsigned long extent_tree_get_size(struct extent_tree* extent_tree);

/*
 * Locking functions for use with extent_tree_iter().  They allow you to
 * lock the tree to iterate over it:
 *
 *    extent_tree_rdlock(&extent_tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(extent_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    extent_tree_unlock(&extent_tree);
 */

/*
 * Lock a extent_tree for reading.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_rdlock(struct extent_tree* extent_tree);

/*
 * Lock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_wrlock(struct extent_tree* extent_tree);

/*
 * Unlock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_unlock(struct extent_tree* extent_tree);

#endif /* __EXTENT_TREE_H__ */
