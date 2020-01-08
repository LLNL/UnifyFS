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

#ifndef UNIFYFS_TREE_H
#define UNIFYFS_TREE_H

#include "abt.h"

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
void unifyfs_tree_init(
    int rank,         /* rank of calling process */
    int ranks,        /* number of ranks in tree */
    int root,         /* rank of root process */
    int k,            /* degree of k-ary tree */
    unifyfs_tree_t* t /* output tree structure */
);

/* free resources allocated in unifyfs_tree_init */
void unifyfs_tree_free(unifyfs_tree_t* t);

/* this structure tracks the state of an outstanding file size
 * bcast/reduce collective, it is allocated when a new operation
 * is started and freed once the process compeltes its portion
 * of the operation */
typedef struct {
  int root;            /* root of the tree (server making request) */
  unifyfs_tree_t tree; /* tree structure for given root */
  int gfid;            /* global file id of request */
  int32_t parent_tag;  /* tag we use in our reply to our parent */
  int32_t tag;         /* tag children will use in their replies to us */
  int num_responses;   /* number of replies we have received from children */
  size_t filesize;     /* running max of file size */
  int err;             /* returns UNIFYFS_SUCCESS/ERROR code on operation */
  ABT_mutex mutex;     /* argobots mutex to protect condition variable below */
  ABT_cond cond;       /* argobots condition variable root server waits on */
} unifyfs_state_filesize_t;

/* allocate a structure defining the state for a file size collective */
unifyfs_state_filesize_t* state_filesize_alloc(
    int root,     /* rank of server making request */
    int gfid,     /* global file id of request */
    int32_t ptag, /* tag to use when sending replies to parent */
    int32_t tag   /* tag out children should use when sending to us */
);

/* free structure allocated in call to state_filesize_alloc,
 * caller should pass address of pointer to structure,
 * which the call with set to NULL */
void state_filesize_free(unifyfs_state_filesize_t** pst);

/* forward file size request to children */
void filesize_request_forward(unifyfs_state_filesize_t* st);

#endif /* UNIFYFS_TREE_H */
