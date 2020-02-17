#ifndef __GFID2EXT_TREE_H__
#define __GFID2EXT_TREE_H__
#include <assert.h>
#include <pthread.h>
#include "tree.h"
#include "extent_tree.h"
#include "unifyfs_meta.h"

struct gfid2ext_tree_node {
    RB_ENTRY(gfid2ext_tree_node) gfid2extentry;
    int gfid;                    /* global file id */
    unifyfs_file_attr_t attr;    /* file attribute (stat) */
    pthread_rwlock_t rwlock;     /* rw lock for accessing @attr */
    struct extent_tree* extents; /* extents for given file */
};

struct gfid2ext_tree {
    RB_HEAD(gfid2exttree, gfid2ext_tree_node) head;
    pthread_rwlock_t rwlock;
};

/* Returns 0 on success, positive non-zero error code otherwise */
int gfid2ext_tree_init(struct gfid2ext_tree* tree);

/*
 * Remove all nodes in gfid2ext_tree, but keep it initialized so you can
 * gfid2ext_tree_add() to it.
 */
void gfid2ext_tree_clear(struct gfid2ext_tree* tree);

/*
 * Remove and free all nodes in the gfid2ext_tree.
 */
void gfid2ext_tree_destroy(struct gfid2ext_tree* tree);

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int gfid2ext_tree_metaset(
    struct gfid2ext_tree* tree, /* tree to add new extent item */
    int gfid,                   /* global file id */
    int create,                 /* is this creating a new entry? */
    unifyfs_file_attr_t *attr   /* initial file attribute */
);

int gfid2ext_tree_metaget(
    struct gfid2ext_tree* tree, /* tree to add new extent item */
    int gfid,                   /* global file id */
    unifyfs_file_attr_t *attr   /* initial file attribute */
);

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int gfid2ext_tree_add_extent(
    struct gfid2ext_tree* tree, /* tree to add new extent item */
    int gifd,                   /* global file id */
    struct extent_tree* extents /* extent tree for given gfid */
);

/* Search for and return extents for given gfid on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
struct gfid2ext_tree_node* gfid2ext_tree_find(
    struct gfid2ext_tree* tree, /* tree to search */
    int gifd                    /* global file id to find */
);

/* Search for and return extents for given gfid on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
struct extent_tree* gfid2ext_tree_extents(
    struct gfid2ext_tree* tree, /* tree to search */
    int gifd                    /* global file id to find */
);

/* truncate extents to use new maximum, discards extent entries for
 * gfid that exceed the new truncated size, and rewrites any entry
 * that overlaps */
int gfid2ext_tree_truncate(
    struct gfid2ext_tree* tree, /* tree in which to truncate file extents */
    int gfid,                   /* global file id to truncate */
    unsigned long size          /* maximum ending offset of file */
);

/* deletes the entry and associated extent map for a specified file */
int gfid2ext_tree_unlink(
    struct gfid2ext_tree* tree, /* tree in which to unlink file info */
    int gfid                    /* global file id to be deleted */
);

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    gfid2ext_tree_rdlock(tree);
 *
 *    struct gfid2ext_tree_node *node = NULL;
 *    while ((node = gfid2ext_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    gfid2ext_tree_unlock(tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the gfid2ext_tree before doing the iteration (see
 * gfid2ext_tree_rdlock()/gfid2ext_tree_wrlock()/gfid2ext_tree_unlock()).
 */
struct gfid2ext_tree_node* gfid2ext_tree_iter(
    struct gfid2ext_tree* tree,
    struct gfid2ext_tree_node* start);

/*
 * Locking functions for use with gfid2ext_tree_iter().  They allow you to
 * lock the tree to iterate over it:
 *
 *    gfid2ext_tree_rdlock(&tree);
 *
 *    struct gfid2ext_tree_node *node = NULL;
 *    while ((node = gfid2ext_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    gfid2ext_tree_unlock(&tree);
 */

/*
 * Lock a gfid2ext_tree for reading.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_rdlock(struct gfid2ext_tree* tree);

/*
 * Lock a gfid2ext_tree for read/write.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_wrlock(struct gfid2ext_tree* tree);

/*
 * Unlock a gfid2ext_tree for read/write.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_unlock(struct gfid2ext_tree* tree);

static inline
void gfid2ext_tree_node_rdlock(struct gfid2ext_tree_node* n)
{
    assert(pthread_rwlock_rdlock(&n->rwlock) == 0);
}

static inline
void gfid2ext_tree_node_wrlock(struct gfid2ext_tree_node* n)
{
    assert(pthread_rwlock_wrlock(&n->rwlock) == 0);
}

static inline
void gfid2ext_tree_node_unlock(struct gfid2ext_tree_node* n)
{
    assert(pthread_rwlock_unlock(&n->rwlock) == 0);
}

#endif /* __GFID2EXT_TREE_H__ */
