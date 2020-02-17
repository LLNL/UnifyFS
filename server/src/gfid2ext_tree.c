/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 /*
  * This file is a simple, thread-safe, segment tree implementation.  The
  * segments in the tree are non-overlapping.  Added segments overwrite the old
  * segments in the tree.  This is used to coalesce writes before an fsync.
  */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include "tree.h"
#include "extent_tree.h"
#include "gfid2ext_tree.h"

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

static int gfid2ext_compare_func(
    struct gfid2ext_tree_node* node1,
    struct gfid2ext_tree_node* node2)
{
    if (node1->gfid > node2->gfid) {
        return 1;
    } else if (node1->gfid < node2->gfid) {
        return -1;
    } else {
        return 0;
    }
}

RB_PROTOTYPE(
    gfid2exttree, gfid2ext_tree_node,
    gfid2extentry, gfid2ext_compare_func)
RB_GENERATE(
    gfid2exttree, gfid2ext_tree_node,
    gfid2extentry, gfid2ext_compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int gfid2ext_tree_init(struct gfid2ext_tree* tree)
{
    memset(tree, 0, sizeof(*tree));
    pthread_rwlock_init(&tree->rwlock, NULL);
    RB_INIT(&tree->head);

    return 0;
};

/* Remove and free all nodes in the gfid2ext_tree. */
void gfid2ext_tree_destroy(struct gfid2ext_tree* tree)
{
    gfid2ext_tree_clear(tree);
};

/* Allocate a node for the tree.  Free node with free() when finished */
static struct gfid2ext_tree_node* gfid2ext_tree_node_alloc(
    int gfid,                    /* global file id */
    unifyfs_file_attr_t* attr,   /* initial file attribute */
    struct extent_tree* extents) /* extents for gfid */
{
    /* allocate a new node structure */
    struct gfid2ext_tree_node* node;
    node = calloc(1, sizeof(*node));
    if (!node) {
        return NULL;
    }

    /* record file id and pointer to extent tree */
    node->gfid    = gfid;
    node->extents = extents;
    pthread_rwlock_init(&node->rwlock, NULL);

    return node;
}

static int gfid2ext_tree_add_node(
    struct gfid2ext_tree* tree, /* tree on which to add new entry */
    int gfid,                   /* global file id */
    unifyfs_file_attr_t* attr)  /* initial file attribute */
{
    int ret = 0;
    struct gfid2ext_tree_node* node = NULL;
    struct gfid2ext_tree_node* existing = NULL;

    node = gfid2ext_tree_node_alloc(gfid, attr, NULL);
    if (!node) {
        return ENOMEM;
    }
    node->attr = *attr;

    gfid2ext_tree_wrlock(tree);

    /* check if the node already exists */
    existing = RB_FIND(gfid2exttree, &tree->head, node);
    if (existing) {
        free(node);
        ret = EEXIST;
        goto out_unlock;
    }

    RB_INSERT(gfid2exttree, &tree->head, node);

out_unlock:
    gfid2ext_tree_unlock(tree);

    return ret;
}

static inline void gfid2ext_tree_node_update_attr(
    struct gfid2ext_tree_node* node,
    unifyfs_file_attr_t* attr)
{
    gfid2ext_tree_node_wrlock(node);
    /* FIXME: we need to selectively update the attr for any metadata update
     * operations, e.g., chmod, chown, ...
     * Possibly, we could initialize each member of the @attr to be -1, then
     * only update for members having >=0 values.
     */
    node->attr = *attr;
    gfid2ext_tree_node_unlock(node);
}

static int gfid2ext_tree_update_node(
    struct gfid2ext_tree* tree, /* tree on which to add new entry */
    int gfid,                   /* global file id */
    unifyfs_file_attr_t* attr)  /* initial file attribute */
{
    int ret = 0;
    struct gfid2ext_tree_node* existing = NULL;
    struct gfid2ext_tree_node node = { 0, };

    node.gfid = gfid;

    gfid2ext_tree_rdlock(tree);

    /* check if the node already exists */
    existing = RB_FIND(gfid2exttree, &tree->head, &node);
    if (!existing) {
        ret = ENOENT;
        goto unlock_tree;
    }

    gfid2ext_tree_node_update_attr(existing, attr);

unlock_tree:
    gfid2ext_tree_unlock(tree);

    return ret;
}

int gfid2ext_tree_metaset(
    struct gfid2ext_tree* tree, /* tree to add new extent item */
    int gfid,                   /* global file id */
    int create,                 /* is this creating a new entry? */
    unifyfs_file_attr_t *attr)  /* initial file attribute */
{
    int ret = 0;

    if (!tree || !attr)
        return EINVAL;

    if (create)
        ret = gfid2ext_tree_add_node(tree, gfid, attr);
    else
        ret = gfid2ext_tree_update_node(tree, gfid, attr);

    return ret;
}

int gfid2ext_tree_metaget(
    struct gfid2ext_tree* tree, /* tree to add new extent item */
    int gfid,                   /* global file id */
    unifyfs_file_attr_t *attr)  /* initial file attribute */
{
    int ret = 0;
    struct gfid2ext_tree_node* node = NULL;
    struct gfid2ext_tree_node tmp = { .gfid = gfid, };

    if (!tree || !attr)
        return EINVAL;

    gfid2ext_tree_rdlock(tree);

    node = RB_FIND(gfid2exttree, &tree->head, &tmp);
    if (node) {
        *attr = node->attr;
    } else {
        ret = ENOENT;
    }

    gfid2ext_tree_unlock(tree);

    return ret;
}


/* Add an entry to the gfid2exts tree.
 * Returns 0 on success, nonzero otherwise. */
int gfid2ext_tree_add_extent(
    struct gfid2ext_tree* tree, /* tree on which to add new entry */
    int gfid,                      /* global file id */
    struct extent_tree* extents)   /* extents for gfid */
{
    int ret = 0;
    struct gfid2ext_tree_node tmp = { 0, };
    struct gfid2ext_tree_node* node = NULL;

    memset(&tmp, 0, sizeof(tmp));
    tmp.gfid = gfid;

    gfid2ext_tree_rdlock(tree);

    node = RB_FIND(gfid2exttree, &tree->head, &tmp);
    if (!node) {
        ret = ENOENT;
        goto out_unlock;
    }

    node->extents = extents;

out_unlock:
    gfid2ext_tree_unlock(tree);

    return ret;
}

/* Search for and return entry for given gfid on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
struct gfid2ext_tree_node* gfid2ext_tree_find(
    struct gfid2ext_tree* tree, /* tree to search */
    int gfid)                   /* global file id to find */
{
    /* Create our range */
    struct gfid2ext_tree_node node = { 0, };
    node.gfid = gfid;

    /* check that entry for gfid dodes not already exist */
    struct gfid2ext_tree_node* existing = RB_FIND(
        gfid2exttree, &tree->head, &node);

    return existing;
}

/* Search for and return extents for given gfid on specified tree.
 * If not found, return NULL */
struct extent_tree* gfid2ext_tree_extents(
    struct gfid2ext_tree* tree, /* tree to search */
    int gfid)                   /* global file id to find */
{
    struct extent_tree* extents = NULL;

    /* search for entry for givein gfid */
    struct gfid2ext_tree_node* node = gfid2ext_tree_find(tree, gfid);
    if (node != NULL) {
        /* found entry, return pointer to its extent tree */
        extents = node->extents;
    }

    return extents;
}

/* truncate extents to use new maximum, discards extent entries for
 * gfid that exceed the new truncated size, and rewrites any entry
 * that overlaps */
int gfid2ext_tree_truncate(
    struct gfid2ext_tree* tree, /* tree in which to truncate file extents */
    int gfid,                   /* global file id to truncate */
    unsigned long size)         /* maximum ending offset of file */
{
    /* assume we'll succeed */
    int rc = 0;

    /* lock the tree so we can modify it */
    gfid2ext_tree_wrlock(tree);

    /* lookup node for given gfid */
    struct extent_tree* extents = gfid2ext_tree_extents(tree, gfid);
    if (extents != NULL) {
        /* found an entry for the given file,
         * now truncate the extents if we have them */
        extent_tree_truncate(extents, size);
    } else {
        /* failed to find extents for given gfid */
        rc = EEXIST;
    }

    /* done modifying the tree */
    gfid2ext_tree_unlock(tree);

    return rc;
}

/* deletes the entry and associated extent map for a specified file */
int gfid2ext_tree_unlink(
    struct gfid2ext_tree* tree, /* tree in which to unlink file info */
    int gfid)                   /* global file id to be deleted */
{
    /* assume we'll succeed */
    int rc = 0;

    /* lock the tree so we can modify it */
    gfid2ext_tree_wrlock(tree);

    /* Create our range */
    struct gfid2ext_tree_node node = { 0, };
    node.gfid = gfid;

    /* check that entry for gfid dodes not already exist */
    struct gfid2ext_tree_node* existing;
    existing = RB_FIND(gfid2exttree, &tree->head, &node);
    if (existing) {
        /* found a node for this gfid, remove it from the tree */
        RB_REMOVE(gfid2exttree, &tree->head, existing);

        /* free off the extents if we have them */
        if (existing->extents != NULL) {
            extent_tree_destroy(existing->extents);
        }

        /* free off the gfid-to-extent node */
        free(existing);
    } else {
        /* failed to find entry for given gfid */
        rc = EEXIST;
    }

    /* done modifying the tree */
    gfid2ext_tree_unlock(tree);

    return rc;
}

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
    struct gfid2ext_tree_node* start)
{
    struct gfid2ext_tree_node* next = NULL;
    if (start == NULL) {
        /* Initial case, no starting node */
        next = RB_MIN(gfid2exttree, &tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    next = RB_FIND(gfid2exttree, &tree->head, start);
    if (!next) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(gfid2exttree, &tree->head, start);

    return next;
}

/*
 * Lock a gfid2ext_tree for reading.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_rdlock(struct gfid2ext_tree* tree)
{
    assert(pthread_rwlock_rdlock(&tree->rwlock) == 0);
}

/*
 * Lock a gfid2ext_tree for read/write.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_wrlock(struct gfid2ext_tree* tree)
{
    assert(pthread_rwlock_wrlock(&tree->rwlock) == 0);
}

/*
 * Unlock a gfid2ext_tree for read/write.  This should only be used for calling
 * gfid2ext_tree_iter().  All the other gfid2ext_tree functions provide their
 * own locking.
 */
void gfid2ext_tree_unlock(struct gfid2ext_tree* tree)
{
    assert(pthread_rwlock_unlock(&tree->rwlock) == 0);
}

/*
 * Remove all nodes in gfid2ext_tree, but keep it initialized so you can
 * gfid2ext_tree_add() to it.
 */
void gfid2ext_tree_clear(struct gfid2ext_tree* tree)
{
    struct gfid2ext_tree_node* node = NULL;
    struct gfid2ext_tree_node* oldnode = NULL;

    gfid2ext_tree_wrlock(tree);

    if (RB_EMPTY(&tree->head)) {
        /* gfid2ext_tree is empty, nothing to do */
        gfid2ext_tree_unlock(tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = gfid2ext_tree_iter(tree, node))) {
        if (oldnode) {
            RB_REMOVE(gfid2exttree, &tree->head, oldnode);
            if (oldnode->extents != NULL) {
                extent_tree_destroy(oldnode->extents);
            }
            free(oldnode);
        }
        oldnode = node;
    }
    if (oldnode) {
        RB_REMOVE(gfid2exttree, &tree->head, oldnode);
        if (oldnode->extents != NULL) {
            extent_tree_destroy(oldnode->extents);
        }
        free(oldnode);
    }

    gfid2ext_tree_unlock(tree);
}
