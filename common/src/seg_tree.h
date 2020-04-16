#ifndef __SEG_TREE_H__
#define __SEG_TREE_H__
#include <pthread.h>
#include "tree.h"

struct seg_tree_node {
    RB_ENTRY(seg_tree_node) entry;
    unsigned long start; /* starting logical offset of range */
    unsigned long end;   /* ending logical offset of range */
    unsigned long ptr;   /* physical offset of data in log */
};

struct seg_tree {
    RB_HEAD(seg_inttree, seg_tree_node) head;
    pthread_rwlock_t rwlock;
    unsigned long count;     /* number of segments stored in tree */
    unsigned long max;       /* maximum logical offset value in the tree */
};

/* Returns 0 on success, positive non-zero error code otherwise */
int seg_tree_init(struct seg_tree* seg_tree);

/*
 * Remove all nodes in seg_tree, but keep it initialized so you can
 * seg_tree_add() to it.
 */
void seg_tree_clear(struct seg_tree* seg_tree);

/*
 * Remove and free all nodes in the seg_tree.
 */
void seg_tree_destroy(struct seg_tree* seg_tree);

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int seg_tree_add(struct seg_tree* seg_tree, unsigned long start,
    unsigned long end, unsigned long ptr);

/*
 * Find the first seg_tree_node that falls in a [start, end] range.
 */
struct seg_tree_node* seg_tree_find(
    struct seg_tree* seg_tree,
    unsigned long start,
    unsigned long end
);

/*
 * Find the first seg_tree_node that falls in a [start, end] range.
 * Assumes you've already locked the tree.
 */
struct seg_tree_node* seg_tree_find_nolock(
    struct seg_tree* seg_tree,
    unsigned long start,
    unsigned long end
);

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    seg_tree_rdlock(seg_tree);
 *
 *    struct seg_tree_node *node = NULL;
 *    while ((node = seg_tree_iter(seg_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    seg_tree_unlock(seg_tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the seg_tree before doing the iteration (see
 * seg_tree_rdlock()/seg_tree_wrlock()/seg_tree_unlock()).
 */
struct seg_tree_node* seg_tree_iter(struct seg_tree* seg_tree,
    struct seg_tree_node* start);

/* Return the number of segments in the segment tree */
unsigned long seg_tree_count(struct seg_tree* seg_tree);

/* Return the maximum ending logical offset in the tree */
unsigned long seg_tree_max(struct seg_tree* seg_tree);

/*
 * Locking functions for use with seg_tree_iter().  They allow you to lock the
 * tree to iterate over it:
 *
 *    seg_tree_rdlock(&seg_tree);
 *
 *    struct seg_tree_node *node = NULL;
 *    while ((node = seg_tree_iter(seg_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    seg_tree_unlock(&seg_tree);
 */

/*
 * Lock a seg_tree for reading.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void seg_tree_rdlock(struct seg_tree* seg_tree);

/*
 * Lock a seg_tree for read/write.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void seg_tree_wrlock(struct seg_tree* seg_tree);

/*
 * Unlock a seg_tree for read/write.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void seg_tree_unlock(struct seg_tree* seg_tree);

#endif
