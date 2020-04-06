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
void unifyfs_tree_init(
    int rank,         /* rank of calling process */
    int ranks,        /* number of ranks in tree */
    int root,         /* rank of root process */
    int k,            /* degree of k-ary tree */
    unifyfs_tree_t* t /* output tree structure */
);

/* free resources allocated in unifyfs_tree_init */
void unifyfs_tree_free(unifyfs_tree_t* t);

/* the structure that tracks all collective operations */
typedef struct {
  /* tracking collective operations */
  int root;            /* root of the tree (server making request) */
  unifyfs_tree_t tree; /* tree structure for given root */
  int32_t parent_tag;  /* tag we use in our reply to our parent */
  int32_t tag;         /* tag children will use in their replies to us */
  int num_responses;   /* number of replies we have received from children */
  int err;             /* returns UNIFYFS_SUCCESS/ERROR code on operation */
  ABT_mutex mutex;     /* argobots mutex to protect condition variable below */
  ABT_cond cond;       /* argobots condition variable root server waits on */

  /* operation specific information */
  int gfid;                  /* global file id of request */
  size_t filesize;           /* running max of file size */
  int create;                /* is this for creating a new entry? */
  unifyfs_file_attr_t attr;  /* file attribute */
} unifyfs_coll_state_t;

/**
 * @brief allocate a structure defining the state for a file size collective
 *
 * @param root rank of server making request
 * @param gfid global file id of request
 * @param ptag tag to use when sending replies to parent
 * @param tag tag out children should use when sending to us
 * @return unifyfs_coll_state_t*
 */
unifyfs_coll_state_t* unifyfs_coll_state_alloc(
    int root,     /* rank of server making request */
    int gfid,     /* global file id of request */
    int32_t ptag, /* tag to use when sending replies to parent */
    int32_t tag   /* tag out children should use when sending to us */
);

/* free structure allocated in call to state_filesize_alloc,
 * caller should pass address of pointer to structure,
 * which the call with set to NULL */
void unifyfs_coll_state_free(unifyfs_coll_state_t** pst);

/* forward request to children */
void filesize_request_forward(unifyfs_coll_state_t* st);
void truncate_request_forward(unifyfs_coll_state_t* st);
void metaset_request_forward(unifyfs_coll_state_t* st);
void unlink_request_forward(unifyfs_coll_state_t* st);

/**
 * @brief
 *
 * @return int UnifyFS return code
 */
int unifyfs_broadcast_extend_tree(int gfid);

#endif /* UNIFYFS_TREE_H */
