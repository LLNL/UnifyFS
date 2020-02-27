/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/** @file
 * 
 */

#ifndef UNIFYFS_TREE_H
#define UNIFYFS_TREE_H

#include <abt.h>
#include "unifyfs_meta.h"

/* define tree structure */
typedef struct {
    int rank;         /* global rank of calling process */
    int ranks;        /* number of ranks in tree */
    int parent_rank;  /* parent rank, -1 if root */
    int child_count;  /* number of children */
    int* child_ranks; /* list of child ranks */
} unifyfs_tree_t;

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
int unifyfs_tree_init(
    int rank,         /* rank of calling process */
    int ranks,        /* number of ranks in tree */
    int root,         /* rank of root process */
    int k,            /* degree of k-ary tree */
    unifyfs_tree_t* t /* output tree structure */
);

/* free resources allocated in unifyfs_tree_init */
void unifyfs_tree_free(unifyfs_tree_t* t);

#endif /* UNIFYFS_TREE_H */
