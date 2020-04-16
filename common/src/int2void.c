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
#include "int2void.h"

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

static int int2void_compare_func(
    struct int2void_node* node1,
    struct int2void_node* node2)
{
    if (node1->key > node2->key) {
        return 1;
    } else if (node1->key < node2->key) {
        return -1;
    } else {
        return 0;
    }
}

RB_PROTOTYPE(
    int2voidtree, int2void_node,
    int2voidentry, int2void_compare_func)
RB_GENERATE(
    int2voidtree, int2void_node,
    int2voidentry, int2void_compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int int2void_init(struct int2void* tree)
{
    memset(tree, 0, sizeof(*tree));
//    pthread_rwlock_init(&tree->rwlock, NULL);
    RB_INIT(&tree->head);

    return 0;
};

/* Remove and free all nodes in the int2void. */
void int2void_destroy(struct int2void* tree)
{
    int2void_clear(tree);
};

/* Allocate a node for the tree.  Free node with free() when finished */
static struct int2void_node* int2void_node_alloc(
    int key,     /* integer key for lookup */
    void* value) /* pointer to whatever as value */
{
    /* allocate a new node structure */
    struct int2void_node* node;
    node = calloc(1, sizeof(*node));
    if (!node) {
        return NULL;
    }

    /* record key and value on node */
    node->key   = key;
    node->value = value;

    return node;
}

/* Add an entry to the int2voids tree.
 * Returns 0 on success, nonzero otherwise. */
int int2void_add(
    struct int2void* tree, /* tree on which to add new entry */
    int key,                    /* integer key to associate with item */
    void* value)                /* pointer to whatever as value */
{
    /* Create our node to be inserted */
    struct int2void_node* node = int2void_node_alloc(
        key, value);
    if (!node) {
        return ENOMEM;
    }

    /* lock the tree so we can modify it */
//    int2void_wrlock(tree);

    /* check that entry for gfid dodes not already exist */
    struct int2void_node* existing;
    existing = RB_FIND(int2voidtree, &tree->head, node);
    if (existing) {
        /* Found existing entry for given file id */
//        int2void_unlock(tree);
        return EEXIST;
    }

    /* otherwise, insert new entry into tree */
    RB_INSERT(int2voidtree, &tree->head, node);

    /* done modifying the tree */
//    int2void_unlock(tree);

    return 0;
}

/* deletes specified item from given tree,
 * returns 0 on success, nonzero otherwise */
int int2void_delete(
    struct int2void* tree, /* tree to delete item from */
    int key)               /* key of item to be removed */
{
    /* Create our node to search for */
    struct int2void_node* node = int2void_node_alloc(
        key, NULL);
    if (!node) {
        return ENOMEM;
    }

    /* check that entry for gfid dodes not already exist */
    struct int2void_node* existing = RB_FIND(
        int2voidtree, &tree->head, node);

    free(node);

    if (!existing) {
        /* failed to find item */
        return EEXIST;
    }

    /* found the item, now remove it */
    RB_REMOVE(int2voidtree, &tree->head, existing);
    free(existing);

    return 0;
}

/* Search for and return value for given key on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
void* int2void_find(
    struct int2void* tree, /* tree to search */
    int key)                    /* key value to search for */
{
    /* Create our node to search for */
    struct int2void_node* node = int2void_node_alloc(
        key, NULL);
    if (!node) {
        return NULL;
    }

    /* check that entry for gfid dodes not already exist */
    struct int2void_node* existing = RB_FIND(
        int2voidtree, &tree->head, node);

    free(node);

    if (existing) {
        return existing->value;
    }
    return NULL;
}

/*
 * Given a tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    int2void_rdlock(tree);
 *
 *    struct int2void_node *node = NULL;
 *    while ((node = int2void_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    int2void_unlock(tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the int2void before doing the iteration (see
 * int2void_rdlock()/int2void_wrlock()/int2void_unlock()).
 */
struct int2void_node* int2void_iter(
    struct int2void* tree,
    struct int2void_node* start)
{
    struct int2void_node* next = NULL;
    if (start == NULL) {
        /* Initial case, no starting node */
        next = RB_MIN(int2voidtree, &tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    next = RB_FIND(int2voidtree, &tree->head, start);
    if (!next) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(int2voidtree, &tree->head, start);

    return next;
}

#if 0
/*
 * Lock a int2void for reading.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
void int2void_rdlock(struct int2void* tree)
{
    assert(pthread_rwlock_rdlock(&tree->rwlock) == 0);
}

/*
 * Lock a int2void for read/write.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
void int2void_wrlock(struct int2void* tree)
{
    assert(pthread_rwlock_wrlock(&tree->rwlock) == 0);
}

/*
 * Unlock a int2void for read/write.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
void int2void_unlock(struct int2void* tree)
{
    assert(pthread_rwlock_unlock(&tree->rwlock) == 0);
}
#endif

/*
 * Remove all nodes in int2void, but keep it initialized so you can
 * int2void_add() to it.
 */
void int2void_clear(struct int2void* tree)
{
    struct int2void_node* node = NULL;
    struct int2void_node* oldnode = NULL;

//    int2void_wrlock(tree);

    if (RB_EMPTY(&tree->head)) {
        /* int2void is empty, nothing to do */
//        int2void_unlock(tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = int2void_iter(tree, node))) {
        if (oldnode) {
            RB_REMOVE(int2voidtree, &tree->head, oldnode);
            free(oldnode);
        }
        oldnode = node;
    }
    if (oldnode) {
        RB_REMOVE(int2voidtree, &tree->head, oldnode);
        free(oldnode);
    }

//    int2void_unlock(tree);
}
