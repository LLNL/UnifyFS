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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "unifyfs_inode_tree.h"

#undef MIN
#undef MAX
#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

static int uit_compare_func(struct unifyfs_inode* node1,
                            struct unifyfs_inode* node2)
{
    if (node1->gfid > node2->gfid) {
        return 1;
    } else if (node1->gfid < node2->gfid) {
        return -1;
    } else {
        return 0;
    }
}

RB_PROTOTYPE(rb_inode_tree, unifyfs_inode, inode_tree_entry, uit_compare_func)
RB_GENERATE(rb_inode_tree, unifyfs_inode, inode_tree_entry, uit_compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int unifyfs_inode_tree_init(struct unifyfs_inode_tree* tree)
{
    if (NULL == tree) {
        return EINVAL;
    }

    memset(tree, 0, sizeof(*tree));
    ABT_rwlock_create(&(tree->rwlock));
    RB_INIT(&tree->head);

    return UNIFYFS_SUCCESS;
}

/* Remove and free all nodes in the unifyfs_inode_tree. */
void unifyfs_inode_tree_destroy(struct unifyfs_inode_tree* tree)
{
    if (NULL != tree) {
        unifyfs_inode_tree_clear(tree);
        ABT_rwlock_free(&(tree->rwlock));
    }
}

int unifyfs_inode_tree_insert(
    struct unifyfs_inode_tree* tree, /* tree on which to add new entry */
    struct unifyfs_inode* ino)       /* initial file attribute */
{
    struct unifyfs_inode* existing = NULL;

    if ((NULL == ino) || (ino->gfid != ino->attr.gfid)) {
        return EINVAL;
    }

    /* check if the node already exists */
    existing = RB_FIND(rb_inode_tree, &tree->head, ino);
    if (existing) {
        return EEXIST;
    }

    RB_INSERT(rb_inode_tree, &tree->head, ino);

    return UNIFYFS_SUCCESS;
}

/* Search for and return entry for given gfid on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
struct unifyfs_inode* unifyfs_inode_tree_search(
    struct unifyfs_inode_tree* tree,
    int gfid)
{
    struct unifyfs_inode node = { .gfid = gfid, };

    return RB_FIND(rb_inode_tree, &tree->head, &node);
}

int unifyfs_inode_tree_remove(
    struct unifyfs_inode_tree* tree,
    int gfid,
    struct unifyfs_inode** removed)
{
    struct unifyfs_inode* ino = unifyfs_inode_tree_search(tree, gfid);
    if (NULL == ino) {
        return ENOENT;
    }

    RB_REMOVE(rb_inode_tree, &tree->head, ino);

    *removed = ino;

    return UNIFYFS_SUCCESS;
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
 *    struct unifyfs_inode *node = NULL;
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
struct unifyfs_inode* unifyfs_inode_tree_iter(
    struct unifyfs_inode_tree* tree,
    struct unifyfs_inode* start)
{
    struct unifyfs_inode* next = NULL;
    if (start == NULL) {
        /* Initial case, no starting node */
        next = RB_MIN(rb_inode_tree, &tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    next = RB_FIND(rb_inode_tree, &tree->head, start);
    if (!next) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(rb_inode_tree, &tree->head, start);

    return next;
}

/*
 * Remove all nodes in unifyfs_inode_tree, but keep it initialized so you can
 * unifyfs_inode_tree_add() to it.
 */
void unifyfs_inode_tree_clear(
    struct unifyfs_inode_tree* tree)
{
    struct unifyfs_inode* node = NULL;
    struct unifyfs_inode* oldnode = NULL;

    unifyfs_inode_tree_wrlock(tree);

    if (RB_EMPTY(&tree->head)) {
        /* unifyfs_inode_tree is empty, nothing to do */
        unifyfs_inode_tree_unlock(tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = unifyfs_inode_tree_iter(tree, node))) {
        if (oldnode) {
            RB_REMOVE(rb_inode_tree, &tree->head, oldnode);
            int rc = unifyfs_inode_destroy(oldnode);
            if (rc) {
                LOGERR("Error %d from unifyfs_inode_destroy()", rc);
            }
        }
        oldnode = node;
    }
    if (oldnode) {
        RB_REMOVE(rb_inode_tree, &tree->head, oldnode);
        int rc = unifyfs_inode_destroy(oldnode);
        if (rc) {
            LOGERR("Error %d from unifyfs_inode_destroy()", rc);
        }
    }

    unifyfs_inode_tree_unlock(tree);
}

