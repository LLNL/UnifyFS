#ifndef __UNIFYFS_INODE_TREE_H
#define __UNIFYFS_INODE_TREE_H

#include <pthread.h>
#include "tree.h"
#include "extent_tree.h"
#include "unifyfs_meta.h"
#include "unifyfs_inode.h"

/*
 * unifyfs_inode_tree: balanced binary tree (RB) for keeping active inodes.
 *
 * NOTE: except for unifyfs_inode_tree_destroy, none of the following functions
 * perform locking itself, but the caller should accordingly lock/unlock using
 * unifyfs_inode_tree_rdlock, unifyfs_inode_tree_wrlock, and
 * unifyfs_inode_tree_unlock.
 */
struct unifyfs_inode_tree {
    RB_HEAD(rb_inode_tree, unifyfs_inode) head;  /** inode RB tree */
    pthread_rwlock_t rwlock;                     /** lock for accessing tree */
};

/**
 * @brief initialize the inode tree.
 *
 * @param tree the tree structure to be initialized. this should be allocated
 * by the caller.
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_tree_init(struct unifyfs_inode_tree* tree);

/**
 * @brief Remove all nodes in unifyfs_inode_tree, but keep it initialized so
 * you can unifyfs_inode_tree_add() to it.
 *
 * @param tree inode tree to remove
 */
void unifyfs_inode_tree_clear(struct unifyfs_inode_tree* tree);

/**
 * @brief Remove and free all nodes in the unifyfs_inode_tree.
 *
 * @param tree inode tree to remove
 */
void unifyfs_inode_tree_destroy(struct unifyfs_inode_tree* tree);

/**
 * @brief Insert a new inode to the tree.
 *
 * @param tree inode tree
 * @param ino new inode to insert
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_tree_insert(struct unifyfs_inode_tree* tree,
                              struct unifyfs_inode* ino);

/**
 * @brief Remove an inode with @gfid.
 *
 * @param tree inode tree
 * @param gfid global file identifier of the target inode
 * @param removed [out] removed inode
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_inode_tree_remove(struct unifyfs_inode_tree* tree,
                              int gfid, struct unifyfs_inode** removed);

/* Search for and return extents for given gfid on specified tree.
 * If not found, return NULL, assumes caller has lock on tree */
struct unifyfs_inode* unifyfs_inode_tree_search(
    struct unifyfs_inode_tree* tree, /* tree to search */
    int gfid                         /* global file id to find */
);

/**
 * @brief Iterate the inode tree.
 *
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    unifyfs_inode_tree_rdlock(tree);
 *
 *    struct unifyfs_inode *node = NULL;
 *    while ((node = unifyfs_inode_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    unifyfs_inode_tree_unlock(tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the unifyfs_inode_tree before doing the iteration; see:
 * unifyfs_inode_tree_rdlock(), unifyfs_inode_tree_wrlock(),
 * unifyfs_inode_tree_unlock().
 *
 * @param tree inode tree to iterate
 * @param start the starting node
 *
 * @return inode structure
 */
struct unifyfs_inode* unifyfs_inode_tree_iter(struct unifyfs_inode_tree* tree,
                                              struct unifyfs_inode* start);

/*
 * Locking functions for use with unifyfs_inode_tree_iter().  They allow you to
 * lock the tree to iterate over it:
 *
 *    unifyfs_inode_tree_rdlock(&tree);
 *
 *    struct unifyfs_inode *node = NULL;
 *    while ((node = unifyfs_inode_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    unifyfs_inode_tree_unlock(&tree);
 */

/**
 * @brief Lock a unifyfs_inode_tree for reading.  This should only be used for
 * calling unifyfs_inode_tree_iter().  All the other unifyfs_inode_tree
 * functions provide their own locking.
 *
 * @param tree inode tree
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_tree_rdlock(struct unifyfs_inode_tree* tree)
{
    return pthread_rwlock_rdlock(&tree->rwlock);
}

/**
 * @brief Lock a unifyfs_inode_tree for read/write.  This should only be used
 * for calling unifyfs_inode_tree_iter().  All the other unifyfs_inode_tree
 * functions provide their own locking.
 *
 * @param tree inode tree
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_tree_wrlock(struct unifyfs_inode_tree* tree)
{
    return pthread_rwlock_wrlock(&tree->rwlock);
}

/**
 * @brief Unlock a unifyfs_inode_tree for read/write.  This should only be used
 * for calling unifyfs_inode_tree_iter().  All the other unifyfs_inode_tree
 * functions provide their own locking.
 *
 * @param tree inode tree
 */
static inline void unifyfs_inode_tree_unlock(struct unifyfs_inode_tree* tree)
{
    pthread_rwlock_unlock(&tree->rwlock);
}

#endif /* __UNIFYFS_INODE_TREE_H */

