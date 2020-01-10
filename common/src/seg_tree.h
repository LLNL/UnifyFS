#ifndef __SEG_TREE_H__
#define __SEG_TREE_H__
#include <pthread.h>
#include "tree.h"

struct seg_tree_node {
    RB_ENTRY(seg_tree_node) entry;
	unsigned long start, end;    /* our range */
	unsigned long ptr;  /* pointer to our data buffer */
};

struct seg_tree {
    RB_HEAD(inttree, seg_tree_node) head;
    pthread_rwlock_t rwlock;
    unsigned long count;    /* number of segments */
    long max;               /* maximum segment value in the tree */
};

int seg_tree_init(struct seg_tree* seg_tree);
void seg_tree_clear(struct seg_tree* seg_tree);
void seg_tree_destroy(struct seg_tree* seg_tree);
int seg_tree_add(struct seg_tree* seg_tree, unsigned long start,
    unsigned long end, unsigned long ptr);

struct seg_tree_node* seg_tree_iter(struct seg_tree* seg_tree,
    struct seg_tree_node* start);

unsigned long seg_tree_count(struct seg_tree* seg_tree);
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
void seg_tree_rdlock(struct seg_tree* seg_tree);
void seg_tree_wrlock(struct seg_tree* seg_tree);
void seg_tree_unlock(struct seg_tree* seg_tree);

#endif
