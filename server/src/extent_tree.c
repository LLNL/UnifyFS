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

/*
 * This file is a simple, thread-safe, segment tree implementation.  The
 * segments in the tree are non-overlapping.  Added segments overwrite the old
 * segments in the tree.  This is used to coalesce writes before an fsync.
 */

#include "extent_tree.h"

#undef MIN
#define MIN(a, b) (a < b ? a : b)
#undef MAX
#define MAX(a, b) (a > b ? a : b)

int etn_compare_func(struct extent_tree_node* node1,
                     struct extent_tree_node* node2)
{
    if (node1->extent.start > node2->extent.end) {
        return 1;
    } else if (node1->extent.end < node2->extent.start) {
        return -1;
    } else {
        /* any overlap is considered as "equal" */
        return 0;
    }
}

RB_PROTOTYPE(ext_tree, extent_tree_node, entry, etn_compare_func)
RB_GENERATE(ext_tree, extent_tree_node, entry, etn_compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int extent_tree_init(struct extent_tree* tree)
{
    memset(tree, 0, sizeof(*tree));
    ABT_rwlock_create(&(tree->rwlock));
    RB_INIT(&(tree->head));
    return 0;
}

/*
 * Remove and free all nodes in the extent_tree.
 */
void extent_tree_destroy(struct extent_tree* tree)
{
    extent_tree_clear(tree);
    ABT_rwlock_free(&(tree->rwlock));
}

/* Allocate a node for the range tree.  Free node with free() when finished */
static
struct extent_tree_node* extent_tree_node_alloc(extent_metadata* extent)
{
    /* allocate a new node structure */
    struct extent_tree_node* node = calloc(1, sizeof(*node));
    if (NULL != node) {
        memcpy(&(node->extent), extent, sizeof(*extent));
    }
    return node;
}

/*
 * Given two start/end ranges, return a new range from start1/end1 that
 * does not overlap start2/end2. The non-overlapping range is stored
 * in range_start/range_end. If there are no non-overlapping ranges,
 * return 1 from this function, else return 0. If there are two
 * non-overlapping ranges, return the first one in range_start/range_end.
 */
static int get_non_overlapping_range(
    unsigned long start1, unsigned long end1,
    unsigned long start2, unsigned long end2,
    unsigned long* range_start, unsigned long* range_end)
{
    /* this function is only called when we know that segment 1
     * and segment 2 overlap with each other, find first portion
     * of segment 1 that does not overlap with segment 2, if any */
    if (start1 < start2) {
        /* Segment 1 includes a portion before segment 2 starts.
         * Set range to start/end of that leading portion of segment 1
         *
         * s1-------e1
         *     s2--------e2
         *   ---- non-overlap
         */
        *range_start = start1;
        *range_end   = start2 - 1;
        return 0;
    } else if (end1 > end2) {
        /* Segment 1 does not start before segment 2,
         * but segment 1 extends past end of segment 2.
         * Set range to start/end of trailing portion of segment 1
         *
         *       s1-----e1
         *  s2-------e2
         *           --- non-overlap
         */
        *range_start = end2 + 1;
        *range_end   = end1;
        return 0;
    }

    /* Segment 2 completely envelops segment 1.
     * Return 1 to indicate this case
     *
     *    s1-------e1
     * s2-------------e2
     */
    return 1;
}

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int extent_tree_add(struct extent_tree* tree,
                    struct extent_metadata* extent)
{
    /* assume we'll succeed */
    int ret = 0;

    /* Create node to define our new range */
    struct extent_tree_node* node = extent_tree_node_alloc(extent);
    if (!node) {
        return ENOMEM;
    }

    /* lock the tree so we can modify it */
    extent_tree_wrlock(tree);

    /* Try to insert our range into the RB tree.  If it overlaps with any other
     * range, then it is not inserted, and the overlapping range node is
     * returned in 'conflict'.  If 'conflict' is NULL, then there were no
     * overlaps, and our range was successfully inserted. */
    struct extent_tree_node* conflict;
    while ((conflict = RB_INSERT(ext_tree, &tree->head, node)) != NULL) {
        /* Our range overlaps with another range (in 'conflict'). Is there any
         * any part of 'conflict' that is outside our range?  If so, delete
         * the old 'conflict' and insert the smaller, non-overlapping range */
        unsigned long new_start = 0;
        unsigned long new_end   = 0;
        unsigned long new_pos   = 0;
        int rc = get_non_overlapping_range(conflict->extent.start,
                                           conflict->extent.end,
                                           extent->start, extent->end,
                                           &new_start, &new_end);
        if (rc) {
            /* The new range we are adding completely covers the existing
             * range in the tree defined in 'conflict'.
             * Delete the existing range. */
            RB_REMOVE(ext_tree, &tree->head, conflict);
            free(conflict);
            tree->count--;
        } else {
            /* Part of the old range 'conflict' was non-overlapping. Create a
             * new smaller extent for that non-overlap portion. */
            new_pos = conflict->extent.log_pos +
                      (new_start - conflict->extent.start);
            extent_metadata non_overlap_extent = {
                .start    = new_start,
                .end      = new_end,
                .log_pos  = new_pos,
                .svr_rank = conflict->extent.svr_rank,
                .app_id   = conflict->extent.app_id,
                .cli_id   = conflict->extent.cli_id
            };
            struct extent_tree_node* non_overlap =
                extent_tree_node_alloc(&non_overlap_extent);
            if (NULL == non_overlap) {
                /* failed to allocate memory for range node,
                 * bail out and release lock without further
                 * changing state of extent tree */
                free(node);
                ret = ENOMEM;
                goto release_add;
            }

            /* If the non-overlapping part came from the front portion of
             * 'conflict', create an extent that covers the tail portion */
            struct extent_tree_node* conflict_tail = NULL;
            if (non_overlap_extent.end < conflict->extent.end) {
                new_start = non_overlap_extent.end + 1;
                new_end   = conflict->extent.end;
                new_pos   = conflict->extent.log_pos +
                            (new_start - conflict->extent.start);
                extent_metadata tail_extent = {
                    .start    = new_start,
                    .end      = new_end,
                    .log_pos  = new_pos,
                    .svr_rank = conflict->extent.svr_rank,
                    .app_id   = conflict->extent.app_id,
                    .cli_id   = conflict->extent.cli_id
                };
                conflict_tail = extent_tree_node_alloc(&tail_extent);
                if (NULL == conflict_tail) {
                    /* failed to allocate memory for range node,
                     * bail out and release lock without further
                     * changing state of extent tree */
                    free(node);
                    free(non_overlap);
                    ret = ENOMEM;
                    goto release_add;
                }
            }

            /* Remove old range 'conflict' and release it */
            RB_REMOVE(ext_tree, &tree->head, conflict);
            free(conflict);
            tree->count--;

            /* Insert the non-overlapping part of the old range */
            RB_INSERT(ext_tree, &tree->head, non_overlap);
            tree->count++;

            /* If we have a tail portion, insert it and increase our extent
             * count since we just turned one range entry into two */
            if (conflict_tail != NULL) {
                RB_INSERT(ext_tree, &tree->head, conflict_tail);
                tree->count++;
            }
        }
    }

    /* increment segment count in the tree for the
     * new range we just added */
    tree->count++;

    /* update max ending offset if end of new range
     * we just inserted is larger */
    tree->max = MAX(tree->max, extent->end);

    /* get temporary pointer to the node we just added */
    struct extent_tree_node* target = node;

    /* check whether we can coalesce new extent with any preceding extent */
    struct extent_tree_node* prev = RB_PREV(ext_tree, &tree->head, target);
    if ((NULL != prev) && (prev->extent.end + 1 == target->extent.start)) {
        /* found an extent that ends just before the new extent starts,
         * check whether they are also contiguous in the log */
        unsigned long pos_next = prev->extent.log_pos +
                                 extent_length(&(prev->extent));
        if ((prev->extent.svr_rank == target->extent.svr_rank) &&
            (prev->extent.cli_id   == target->extent.cli_id)   &&
            (prev->extent.app_id   == target->extent.app_id)   &&
            (pos_next              == target->extent.log_pos)) {
            /* the preceding extent describes a log position adjacent to
             * the extent we just added, so we can merge them,
             * append entry to previous by extending end of previous */
            prev->extent.end = target->extent.end;

            /* delete new extent from the tree and free it */
            RB_REMOVE(ext_tree, &tree->head, target);
            free(target);
            tree->count--;

            /* update target to point at previous extent since we just
             * merged our new extent into it */
            target = prev;
        }
    }

    /* check whether we can coalesce new extent with any trailing extent */
    struct extent_tree_node* next = RB_NEXT(ext_tree, &tree->head,
                                            target);
    if ((NULL != next) && (target->extent.end + 1 == next->extent.start)) {
        /* found a extent that starts just after the new extent ends,
         * check whether they are also contiguous in the log */
        unsigned long pos_next = target->extent.log_pos +
                                 extent_length(&(target->extent));
        if (target->extent.svr_rank == next->extent.svr_rank &&
            target->extent.cli_id   == next->extent.cli_id   &&
            target->extent.app_id   == next->extent.app_id   &&
            pos_next                == next->extent.log_pos) {
            /* the target extent describes a log position adjacent to
             * the next extent, so we can merge them,
             * append entry to target by extending end of to cover next */
            target->extent.end = next->extent.end;

            /* delete next extent from the tree and free it */
            RB_REMOVE(ext_tree, &tree->head, next);
            free(next);
            tree->count--;
        }
    }

release_add:

    /* done modifying the tree */
    extent_tree_unlock(tree);

    return ret;
}

/* search tree for entry that overlaps with given start/end
 * offsets, return first overlapping entry if found, NULL otherwise,
 * assumes caller has lock on tree */
struct extent_tree_node* extent_tree_find(
    struct extent_tree* tree, /* tree to search */
    unsigned long start, /* starting offset to search */
    unsigned long end)   /* ending offset to search */
{
    /* Create a range of just our starting byte offset */
    extent_metadata start_byte = {
        .start    = start,
        .end      = start,
        .log_pos  = 0,
        .svr_rank = 0,
        .app_id   = 0,
        .cli_id   = 0
    };
    struct extent_tree_node* node = extent_tree_node_alloc(&start_byte);
    if (NULL == node) {
        return NULL;
    }

    /* search tree for either a range that overlaps with
     * the target range (starting byte), or otherwise the
     * node for the next biggest starting byte */
    struct extent_tree_node* next = RB_NFIND(ext_tree, &tree->head, node);

    free(node);

    /* we may have found a node that doesn't include our starting
     * byte offset, but it would be the range with the lowest
     * starting offset after the target starting offset, check whether
     * this overlaps our end offset */
    if ((NULL != next) && (next->extent.start <= end)) {
        return next;
    }

    /* otherwise, there is not element that overlaps with the
     * target range of [start, end] */
    return NULL;
}

/* truncate extents to use new maximum, discards extent entries
 * that exceed the new truncated size, and rewrites any entry
 * that overlaps */
int extent_tree_truncate(
    struct extent_tree* tree, /* tree to truncate */
    unsigned long size)       /* size to truncate extents to */
{
    if (0 == size) {
        extent_tree_clear(tree);
        return 0;
    }

    /* lock the tree */
    extent_tree_wrlock(tree);

    /* lookup node with the extent that has the maximum offset */
    struct extent_tree_node* node = RB_MAX(ext_tree, &tree->head);

    /* iterate backwards until we find an extent below
     * the truncated size */
    while ((NULL != node) && (node->extent.end >= size)) {
        /* found an extent whose ending offset is equal to or
         * extends beyond the truncated size.
         * check whether the full extent is beyond the truncated
         * size or whether the new size falls within this extent */
        if (node->extent.start >= size) {
            /* the start offset is also beyond the truncated size,
             * meaning the entire range is beyond the truncated size.
             * get pointer to previous extent in tree */
            struct extent_tree_node* oldnode = node;
            node = RB_PREV(ext_tree, &tree->head, node);

            /* remove this node from the tree and release it */
            LOGDBG("removing node [%lu, %lu] due to truncate=%lu",
                   node->extent.start, node->extent.end, size);
            RB_REMOVE(ext_tree, &tree->head, oldnode);
            free(oldnode);

            /* decrement the number of extents in the tree */
            tree->count--;
        } else {
            /* the range of this node overlaps with the truncated size
             * so just update its end to be the new last byte offset */
            unsigned long last_byte = size - 1;
            node->extent.end = last_byte;
            break;
        }
    }

    /* update maximum offset in tree */
    if (node != NULL) {
        /* got at least one extent left, update maximum field */
        tree->max = node->extent.end;
    } else {
        /* no extents left in the tree, set max back to 0 */
        tree->max = 0;
    }

    /* done updating the tree */
    extent_tree_unlock(tree);

    return 0;
}

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    extent_tree_rdlock(tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(tree, node))) {
 *       printf("[%d-%d]", node->extent.start, node->extent.end);
 *    }
 *
 *    extent_tree_unlock(tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the extent_tree before doing the iteration (see
 * extent_tree_rdlock()/extent_tree_wrlock()/extent_tree_unlock()).
 */
struct extent_tree_node* extent_tree_iter(
    struct extent_tree* tree,
    struct extent_tree_node* start)
{
    struct extent_tree_node* next = NULL;
    if (NULL == start) {
        /* Initial case, no starting node */
        next = RB_MIN(ext_tree, &tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    next = RB_FIND(ext_tree, &tree->head, start);
    if (NULL == next) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(ext_tree, &tree->head, start);

    return next;
}

/*
 * Lock a extent_tree for reading.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_rdlock(struct extent_tree* tree)
{
    int rc = ABT_rwlock_rdlock(tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_rdlock() failed - rc=%d", rc);
    }
}

/*
 * Lock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_wrlock(struct extent_tree* tree)
{
    int rc = ABT_rwlock_wrlock(tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_wrlock() failed - rc=%d", rc);
    }
}

/*
 * Unlock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_unlock(struct extent_tree* tree)
{
    int rc = ABT_rwlock_unlock(tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_unlock() failed - rc=%d", rc);
    }
}

/*
 * Remove all nodes in extent_tree, but keep it initialized so you can
 * extent_tree_add() to it.
 */
void extent_tree_clear(struct extent_tree* tree)
{
    struct extent_tree_node* node = NULL;
    struct extent_tree_node* oldnode = NULL;

    extent_tree_wrlock(tree);

    if (RB_EMPTY(&tree->head)) {
        /* extent_tree is empty, nothing to do */
        extent_tree_unlock(tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = extent_tree_iter(tree, node)) != NULL) {
        if (NULL != oldnode) {
            RB_REMOVE(ext_tree, &tree->head, oldnode);
            free(oldnode);
        }
        oldnode = node;
    }
    if (NULL != oldnode) {
        RB_REMOVE(ext_tree, &tree->head, oldnode);
        free(oldnode);
    }

    tree->count = 0;
    tree->max   = 0;
    extent_tree_unlock(tree);
}

/* Return the number of segments in the segment tree */
unsigned long extent_tree_count(struct extent_tree* tree)
{
    extent_tree_rdlock(tree);
    unsigned long count = tree->count;
    extent_tree_unlock(tree);
    return count;
}

/* Return the maximum ending logical offset in the tree */
unsigned long extent_tree_max_offset(struct extent_tree* tree)
{
    extent_tree_rdlock(tree);
    unsigned long max = tree->max;
    extent_tree_unlock(tree);
    return max;
}

static void chunk_req_from_extent(
    unsigned long req_offset,
    unsigned long req_len,
    struct extent_tree_node* n,
    chunk_read_req_t* chunk)
{
    unsigned long offset     = n->extent.start;
    unsigned long nbytes     = n->extent.end - n->extent.start + 1;
    unsigned long log_offset = n->extent.log_pos;
    unsigned long last       = req_offset + req_len - 1;

    unsigned long diff;
    if (offset < req_offset) {
        diff = req_offset - offset;
        offset = req_offset;
        log_offset += diff;
        nbytes -= diff;
    }

    if (n->extent.end > last) {
        diff = n->extent.end - last;
        nbytes -= diff;
    }

    chunk->offset        = offset;
    chunk->nbytes        = nbytes;
    chunk->log_offset    = log_offset;
    chunk->rank          = n->extent.svr_rank;
    chunk->log_client_id = n->extent.cli_id;
    chunk->log_app_id    = n->extent.app_id;
}

int extent_tree_get_chunk_list(
    struct extent_tree* tree,  /* extent tree to search */
    unsigned long offset,      /* starting logical offset */
    unsigned long len,         /* length of extent */
    unsigned int* n_chunks,    /* [out] number of chunks returned */
    chunk_read_req_t** chunks, /* [out] chunk array */
    int* extent_covered)       /* [out] set=1 if extent fully covered */
{
    int ret = 0;
    unsigned int count = 0;
    unsigned long end = offset + len - 1;
    struct extent_tree_node* first = NULL;
    struct extent_tree_node* last = NULL;
    struct extent_tree_node* next = NULL;
    chunk_read_req_t* out_chunks = NULL;
    chunk_read_req_t* current = NULL;
    unsigned long prev_end = 0;
    bool gap_found = false;

    *extent_covered = 0;

    extent_tree_rdlock(tree);

    first = extent_tree_find(tree, offset, end);
    next = first;
    while ((NULL != next) && next->extent.start <= end) {
        count++;

        if (!gap_found) {
            unsigned long curr_start = next->extent.start;
            if (next != first) {
                /* check for a gap between current and previous extent */
                if ((prev_end + 1) != curr_start) {
                    gap_found = true;
                }
            }
            prev_end = next->extent.end;
        }

        /* iterate to next extent */
        last = next;
        next = extent_tree_iter(tree, next);
    }

    *n_chunks = count;
    if (0 == count) {
        gap_found = true;
        goto out_unlock;
    } else {
        if ((first->extent.start > offset) || (last->extent.end < end)) {
            gap_found = true;
        }
    }

    out_chunks = calloc(count, sizeof(*out_chunks));
    if (NULL == out_chunks) {
        ret = ENOMEM;
        goto out_unlock;
    }

    next = first;
    current = out_chunks;
    while ((NULL != next) && (next->extent.start <= end)) {
        /* trim out the extent so it does not include the data that is not
         * requested */
        chunk_req_from_extent(offset, len, next, current);

        next = extent_tree_iter(tree, next);
        current += 1;
    }

    *chunks = out_chunks;

out_unlock:
    extent_tree_unlock(tree);

    if (!gap_found) {
        *extent_covered = 1;
    }

    return ret;
}

