#ifndef __INT2VOID_TREE_H__
#define __INT2VOID_TREE_H__
#include <pthread.h>
#include "tree.h"

struct int2void_node {
    RB_ENTRY(int2void_node) int2voidentry;
    int key;     /* integer key to lookup values */
    void* value; /* pointer to whatever */
};

struct int2void {
    RB_HEAD(int2voidtree, int2void_node) head;
    //pthread_rwlock_t rwlock;
};

/* Returns 0 on success, positive non-zero error code otherwise */
int int2void_init(struct int2void* tree);

/*
 * Remove all nodes in int2void, but keep it initialized so you can
 * int2void_add() to it.
 */
void int2void_clear(struct int2void* tree);

/*
 * Remove and free all nodes in the int2void.
 */
void int2void_destroy(struct int2void* tree);

/*
 * Add an entry to the tree.  Returns 0 on success, nonzero otherwise.
 */
int int2void_add(
    struct int2void* tree, /* tree to add new extent item */
    int key,               /* integer key used for lookup */
    void* value            /* pointer to whatever */
);

/* deletes specified item from given tree,
 * returns 0 on success, nonzero otherwise */
int int2void_delete(
    struct int2void* tree, /* tree to delete item from */
    int key                /* key of item to be removed */
);

/* Search for and return node for given key on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
void* int2void_find(
    struct int2void* tree, /* tree to search */
    int key                /* key to search find */
);

/*
 * Locking functions for use with int2void_iter().  They allow you to
 * lock the tree to iterate over it:
 *
 *    int2void_rdlock(&tree);
 *
 *    struct int2void_node *node = NULL;
 *    while ((node = int2void_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    int2void_unlock(&tree);
 */

/*
 * Lock a int2void for reading.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
//void int2void_rdlock(struct int2void* tree);

/*
 * Lock a int2void for read/write.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
//void int2void_wrlock(struct int2void* tree);

/*
 * Unlock a int2void for read/write.  This should only be used for calling
 * int2void_iter().  All the other int2void functions provide their
 * own locking.
 */
//void int2void_unlock(struct int2void* tree);

#endif /* __INT2VOID_TREE_H__ */
