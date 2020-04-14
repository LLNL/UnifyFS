#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "unifyfs_tree.h"


/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
/**
 * @brief given the process's rank and the number of ranks, this computes a k-ary
 *        tree rooted at rank 0, the structure records the number of children
 *        of the local rank and the list of their ranks
 *
 * @param rank rank of calling process
 * @param ranks number of ranks in tree
 * @param root rank of root of tree
 * @param k degree of k-ary tree
 * @param t output tree structure
 */
int unifyfs_tree_init(
    int rank,          /* rank of calling process */
    int ranks,         /* number of ranks in tree */
    int root,          /* rank of root of tree */
    int k,             /* degree of k-ary tree */
    unifyfs_tree_t* t) /* output tree structure */
{
    int i;

    /* compute distance from our rank to root,
     * rotate ranks to put root as rank 0 */
    rank -= root;
    if (rank < 0) {
        rank += ranks;
    }

    /* compute parent and child ranks with root as rank 0 */

    /* initialize fields */
    t->rank        = rank;
    t->ranks       = ranks;
    t->parent_rank = -1;
    t->child_count = 0;
    t->child_ranks = NULL;

    /* compute the maximum number of children this task may have */
    int max_children = k;

    /* allocate memory to hold list of children ranks */
    if (max_children > 0) {
        size_t bytes = (size_t)max_children * sizeof(int);
        t->child_ranks = (int*) malloc(bytes);

        if (t->child_ranks == NULL) {
            //LOGERR("Failed to allocate memory for child rank array");
            return ENOMEM;
        }
    }

    /* initialize all ranks to NULL */
    for (i = 0; i < max_children; i++) {
        t->child_ranks[i] = -1;
    }

    /* compute rank of our parent if we have one */
    if (rank > 0) {
        t->parent_rank = (rank - 1) / k;
    }

    /* identify ranks of what would be leftmost
     * and rightmost children */
    int left  = rank * k + 1;
    int right = rank * k + k;

    /* if we have at least one child,
     * compute number of children and list of child ranks */
    if (left < ranks) {
        /* adjust right child in case we don't have a full set of k */
        if (right >= ranks) {
            right = ranks - 1;
        }

        /* compute number of children */
        t->child_count = right - left + 1;

        /* fill in rank for each child */
        for (i = 0; i < t->child_count; i++) {
            t->child_ranks[i] = left + i;
        }
    }

    /* rotate tree neighbor ranks to use global ranks */

    /* rotate our rank in tree */
    t->rank += root;
    if (t->rank >= ranks) {
        t->rank -= ranks;
    }

    /* rotate rank of our parent in tree if we have one */
    if (t->parent_rank != -1) {
        t->parent_rank += root;
        if (t->parent_rank >= ranks) {
            t->parent_rank -= ranks;
        }
    }

    /* rotate rank of each child in tree */
    for (i = 0; i < t->child_count; i++) {
        t->child_ranks[i] += root;
        if (t->child_ranks[i] >= ranks) {
            t->child_ranks[i] -= ranks;
        }
    }

    return 0;
}

void unifyfs_tree_free(unifyfs_tree_t* t)
{
    /* free child rank list */
    free(t->child_ranks);
    t->child_ranks = NULL;

    return;
}

