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
#include "extent_tree.h"
#include "tree.h"
#include "unifyfs_metadata_mdhim.h"

#undef MIN
#define MIN(a, b) (a < b ? a : b)
#undef MAX
#define MAX(a, b) (a > b ? a : b)

int compare_func(
    struct extent_tree_node* node1,
    struct extent_tree_node* node2)
{
    if (node1->start > node2->end) {
        return 1;
    } else if (node1->end < node2->start) {
        return -1;
    } else {
        return 0;
    }
}

RB_PROTOTYPE(inttree, extent_tree_node, entry, compare_func)
RB_GENERATE(inttree, extent_tree_node, entry, compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int extent_tree_init(struct extent_tree* extent_tree)
{
    memset(extent_tree, 0, sizeof(*extent_tree));
    pthread_rwlock_init(&extent_tree->rwlock, NULL);
    RB_INIT(&extent_tree->head);
    return 0;
}

/*
 * Remove and free all nodes in the extent_tree.
 */
void extent_tree_destroy(struct extent_tree* extent_tree)
{
    extent_tree_clear(extent_tree);
    pthread_rwlock_destroy(&extent_tree->rwlock);
}

/* Allocate a node for the range tree.  Free node with free() when finished */
static struct extent_tree_node* extent_tree_node_alloc(
    unsigned long start, /* logical starting offset of extent */
    unsigned long end,   /* logical ending offset of extent */
    int svr_rank,        /* rank of server hosting data */
    int app_id,          /* application id (namespace) on server rank */
    int cli_id,          /* client rank on server rank */
    unsigned long pos)   /* physical offset of data in log */
{
    /* allocate a new node structure */
    struct extent_tree_node* node = calloc(1, sizeof(*node));
    if (!node) {
        return NULL;
    }

    /* record logical range and physical offset */
    node->start    = start;
    node->end      = end;
    node->svr_rank = svr_rank;
    node->app_id   = app_id;
    node->cli_id   = cli_id;
    node->pos      = pos;

    return node;
}

/*
 * Given two start/end ranges, return a new range from start1/end1 that
 * does not overlap start2/end2.  The non-overlapping range is stored
 * in new_start/new_end.   If there are no non-overlapping ranges,
 * return 1 from this function, else return 0.  If there are two
 * non-overlapping ranges, return the first one in new_start/new_end.
 */
static int get_non_overlapping_range(
    unsigned long start1, unsigned long end1,
    long start2, long end2,
    long* new_start, long* new_end)
{
    /* this function is only called when we know that segment 1
     * and segment 2 overlap with each other, find first portion
     * of segment 1 that does not overlap with segment 2, if any */
    if (start1 < start2) {
        /* Segment 1 inlcudes a portion before segment 2 starts
         * return start/end of that leading portion of segment 1
         *
         * s1-------e1
         *     s2--------e2
         *   ---- non-overlap
         */
        *new_start = start1;
        *new_end   = start2 - 1;
        return 0;
    } else if (end1 > end2) {
        /* Segment 1 does not start before segment 2,
         * but segment 1 extends past end of segment 2
         * return start/end of trailing portion of segment 1
         *
         *       s1-----e1
         *  s2-------e2
         *           --- non-overlap
         */
        *new_start = end2 + 1;
        *new_end   = end1;
        return 0;
    }

    /* Segment 2 completely envelops segment 1
     * nothing left of segment 1 to return
     * so return 1 to indicate this case
     *
     *    s1-------e1
     * s2-------------e2
     */
    return 1;
}

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int extent_tree_add(
    struct extent_tree* extent_tree, /* tree to add new extent item */
    unsigned long start, /* logical starting offset of extent */
    unsigned long end,   /* logical ending offset of extent */
    int svr_rank,        /* rank of server hosting data */
    int app_id,          /* application id (namespace) on server rank */
    int cli_id,          /* client rank on server rank */
    unsigned long pos)   /* physical offset of data in log */
{
    /* assume we'll succeed */
    int rc = 0;

    /* Create node to define our new range */
    struct extent_tree_node* node = extent_tree_node_alloc(
        start, end, svr_rank, app_id, cli_id, pos);
    if (!node) {
        return ENOMEM;
    }

    /* lock the tree so we can modify it */
    extent_tree_wrlock(extent_tree);

    /* Try to insert our range into the RB tree.  If it overlaps with any other
     * range, then it is not inserted, and the overlapping range node is
     * returned in 'overlap'.  If 'overlap' is NULL, then there were no
     * overlaps, and our range was successfully inserted. */
    struct extent_tree_node* overlap;
    while ((overlap = RB_INSERT(inttree, &extent_tree->head, node))) {
        /* Our range overlaps with another range (in 'overlap'). Is there any
         * any part of 'overlap' that does not overlap our range?  If so,
         * delete the old 'overlap' and insert the smaller, non-overlapping
         * range. */
        long new_start = 0;
        long new_end   = 0;
        int ret = get_non_overlapping_range(overlap->start, overlap->end,
            start, end, &new_start, &new_end);
        if (ret) {
            /* The new range we are adding completely covers the existing
             * range in the tree defined in overlap.
             * We can't find a non-overlapping range.
             * Delete the existing range. */
            RB_REMOVE(inttree, &extent_tree->head, overlap);
            free(overlap);
            extent_tree->count--;
        } else {
            /* Part of the old range was non-overlapping.  Split the old range
             * into two ranges: one for the non-overlapping section, and one for
             * the remaining section.  The non-overlapping section gets
             * inserted without issue.  The remaining section will be processed
             * on the next pass of this while() loop. */
            struct extent_tree_node* resized = extent_tree_node_alloc(
                new_start, new_end,
                overlap->svr_rank, overlap->app_id, overlap->cli_id,
                overlap->pos + (new_start - overlap->start));
            if (!resized) {
                /* failed to allocate memory for range node,
                 * bail out and release lock without further
                 * changing state of extent tree */
                free(node);
                rc = ENOMEM;
                goto release_add;
            }

            /* if the non-overlapping part came from the front
             * portion of the existing range, then there is a
             * trailing portion of the existing range to add back
             * to be considered again in the next loop iteration */
            struct extent_tree_node* remaining = NULL;
            if (resized->end < overlap->end) {
                /* There's still a remaining section after the non-overlapping
                 * part.  Add it in. */
                remaining = extent_tree_node_alloc(
                    resized->end + 1, overlap->end,
                    overlap->svr_rank, overlap->app_id, overlap->cli_id,
                    overlap->pos + (resized->end + 1 - overlap->start));
                if (!remaining) {
                    /* failed to allocate memory for range node,
                     * bail out and release lock without further
                     * changing state of extent tree */
                    free(node);
                    free(resized);
                    rc = ENOMEM;
                    goto release_add;
                }
            }

            /* Remove our old range and release it */
            RB_REMOVE(inttree, &extent_tree->head, overlap);
            free(overlap);
            extent_tree->count--;

            /* Insert the non-overlapping part of the new range */
            RB_INSERT(inttree, &extent_tree->head, resized);
            extent_tree->count++;

            /* if we have a trailing portion, insert range for that,
             * and increase our extent count since we just turned one
             * range entry into two */
            if (remaining != NULL) {
                RB_INSERT(inttree, &extent_tree->head, remaining);
                extent_tree->count++;
            }
        }
    }

    /* increment segment count in the tree for the
     * new range we just added */
    extent_tree->count++;

    /* update max ending offset if end of new range
     * we just inserted is larger */
    extent_tree->max = MAX(extent_tree->max, end);

    /* get temporary pointer to the node we just added */
    struct extent_tree_node* target = node;

    /* check whether we can coalesce new extent with any preceding extent */
    struct extent_tree_node* prev = RB_PREV(
        inttree, &extent_tree->head, target);
    if (prev != NULL && prev->end + 1 == target->start) {
        /* found a extent that ends just before the new extent starts,
         * check whether they are also contiguous in the log */
        unsigned long pos_end = prev->pos + (prev->end - prev->start + 1);
        if (prev->svr_rank == target->svr_rank &&
            prev->cli_id   == target->cli_id   &&
            prev->app_id   == target->app_id   &&
            pos_end        == target->pos) {
            /* the preceding extent describes a log position adjacent to
             * the extent we just added, so we can merge them,
             * append entry to previous by extending end of previous */
            prev->end = target->end;

            /* delete new extent from the tree and free it */
            RB_REMOVE(inttree, &extent_tree->head, target);
            free(target);
            extent_tree->count--;

            /* update target to point at previous extent since we just
             * merged our new extent into it */
            target = prev;
        }
    }

    /* check whether we can coalesce new extent with any trailing extent */
    struct extent_tree_node* next = RB_NEXT(
        inttree, &extent_tree->head, target);
    if (next != NULL && target->end + 1 == next->start) {
        /* found a extent that starts just after the new extent ends,
         * check whether they are also contiguous in the log */
        unsigned long pos_end = target->pos + (target->end - target->start + 1);
        if (target->svr_rank == next->svr_rank &&
            target->cli_id   == next->cli_id   &&
            target->app_id   == next->app_id   &&
            pos_end          == next->pos) {
            /* the target extent describes a log position adjacent to
             * the next extent, so we can merge them,
             * append entry to target by extending end of to cover next */
            target->end = next->end;

            /* delete next extent from the tree and free it */
            RB_REMOVE(inttree, &extent_tree->head, next);
            free(next);
            extent_tree->count--;
        }
    }

release_add:

    /* done modifying the tree */
    extent_tree_unlock(extent_tree);

    return rc;
}

/* search tree for entry that overlaps with given start/end
 * offsets, return first overlapping entry if found, NULL otherwise,
 * assumes caller has lock on tree */
struct extent_tree_node* extent_tree_find(
    struct extent_tree* extent_tree, /* tree to search */
    unsigned long start, /* starting offset to search */
    unsigned long end)   /* ending offset to search */
{
    /* Create a range of just our starting byte offset */
    struct extent_tree_node* node = extent_tree_node_alloc(
        start, start, 0, 0, 0, 0);
    if (!node) {
        return NULL;
    }

    /* search tree for either a range that overlaps with
     * the target range (starting byte), or otherwise the
     * node for the next biggest starting byte */
    struct extent_tree_node* next = RB_NFIND(
        inttree, &extent_tree->head, node);

    free(node);

    /* we may have found a node that doesn't include our starting
     * byte offset, but it would be the range with the lowest
     * starting offset after the target starting offset, check whether
     * this overlaps our end offset */
    if (next && next->start <= end) {
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
    /* lock the tree for reading */
    extent_tree_wrlock(tree);

    /* lookup node with the extent that has the maximum offset */
    struct extent_tree_node* node = RB_MAX(inttree, &tree->head);

    /* iterate backwards until we find an extent below
     * the truncated size */
    while (node != NULL && node->end >= size) {
        /* found an extent whose ending offset is equal to or
         * extends beyond the truncated size,
         * check whether the full extent is beyond the truncated
         * size or whether the new size falls within this extent */
        if (node->start >= size) {
            /* the start offset is also beyond the truncated size,
             * meaning the entire range is beyond the truncated size,
             * get pointer to next previous extent in tree */
            struct extent_tree_node* oldnode = node;
            node = RB_PREV(inttree, &tree->head, node);

            /* remove this node from the tree and release it */
            RB_REMOVE(inttree, &tree->head, oldnode);
            free(oldnode);

            /* decrement the number of extents in the tree */
            tree->count--;
        } else {
            /* the range of this node overlaps with the truncated size
             * so just update its end to be the new size */
            node->end = size - 1;
            break;
        }
    }

    /* update maximum offset in tree */
    if (node != NULL) {
        /* got at least one extent left, update maximum field */
        tree->max = node->end;
    } else {
        /* no extents left in the tree, set max back to -1 */
        tree->max = -1;
    }

    /* done reading the tree */
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
 *    extent_tree_rdlock(extent_tree);
 *
 *    struct extent_tree_node *node = NULL;
 *    while ((node = extent_tree_iter(extent_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    extent_tree_unlock(extent_tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the extent_tree before doing the iteration (see
 * extent_tree_rdlock()/extent_tree_wrlock()/extent_tree_unlock()).
 */
struct extent_tree_node* extent_tree_iter(
    struct extent_tree* extent_tree,
    struct extent_tree_node* start)
{
    struct extent_tree_node* next = NULL;
    if (start == NULL) {
        /* Initial case, no starting node */
        next = RB_MIN(inttree, &extent_tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    next = RB_FIND(inttree, &extent_tree->head, start);
    if (!next) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(inttree, &extent_tree->head, start);

    return next;
}

/*
 * Lock a extent_tree for reading.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_rdlock(struct extent_tree* extent_tree)
{
    assert(pthread_rwlock_rdlock(&extent_tree->rwlock) == 0);
}

/*
 * Lock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_wrlock(struct extent_tree* extent_tree)
{
    assert(pthread_rwlock_wrlock(&extent_tree->rwlock) == 0);
}

/*
 * Unlock a extent_tree for read/write.  This should only be used for calling
 * extent_tree_iter().  All the other extent_tree functions provide their
 * own locking.
 */
void extent_tree_unlock(struct extent_tree* extent_tree)
{
    assert(pthread_rwlock_unlock(&extent_tree->rwlock) == 0);
}

/*
 * Remove all nodes in extent_tree, but keep it initialized so you can
 * extent_tree_add() to it.
 */
void extent_tree_clear(struct extent_tree* extent_tree)
{
    struct extent_tree_node* node = NULL;
    struct extent_tree_node* oldnode = NULL;

    extent_tree_wrlock(extent_tree);

    if (RB_EMPTY(&extent_tree->head)) {
        /* extent_tree is empty, nothing to do */
        extent_tree_unlock(extent_tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = extent_tree_iter(extent_tree, node))) {
        if (oldnode) {
            RB_REMOVE(inttree, &extent_tree->head, oldnode);
            free(oldnode);
        }
        oldnode = node;
    }
    if (oldnode) {
        RB_REMOVE(inttree, &extent_tree->head, oldnode);
        free(oldnode);
    }

    extent_tree->count = 0;
    extent_tree->max   = -1;
    extent_tree_unlock(extent_tree);
}

/* Return the number of segments in the segment tree */
unsigned long extent_tree_count(struct extent_tree* extent_tree)
{
    extent_tree_rdlock(extent_tree);
    unsigned long count = extent_tree->count;
    extent_tree_unlock(extent_tree);
    return count;
}

/* Return the maximum ending logical offset in the tree */
unsigned long extent_tree_max(struct extent_tree* extent_tree)
{
    extent_tree_rdlock(extent_tree);
    unsigned long max = extent_tree->max;
    extent_tree_unlock(extent_tree);
    return max;
}

/* Returns the size of the local extents (local file size) */
unsigned long extent_tree_get_size(struct extent_tree* extent_tree)
{
    extent_tree_rdlock(extent_tree);
    unsigned long max = extent_tree->max + 1;
    extent_tree_unlock(extent_tree);
    return max;
}

/* given an extent tree and starting and ending logical offsets,
 * fill in key/value entries that overlap that range, returns at
 * most max entries starting from lowest starting offset,
 * sets outnum with actual number of entries returned */
int extent_tree_span(
    struct extent_tree* extent_tree, /* extent tree to search */
    int gfid,                        /* global file id we're looking in */
    unsigned long start,             /* starting logical offset */
    unsigned long end,               /* ending logical offset */
    int max,                         /* maximum number of key/vals to return */
    void* _keys,             /* array of length max for output keys */
    void* _vals,             /* array of length max for output values */
    int* outnum)                     /* number of entries returned */
{
    unifyfs_key_t* keys = (unifyfs_key_t*) _keys;
    unifyfs_val_t* vals = (unifyfs_val_t*) _vals;

    /* initialize output parameters */
    *outnum = 0;

    /* lock the tree for reading */
    extent_tree_rdlock(extent_tree);

    int count = 0;
    struct extent_tree_node* next = extent_tree_find(extent_tree, start, end);
    while (next != NULL       &&
           next->start <= end &&
           count < max) {
        /* got an entry that overlaps with given span */

        /* fill in key */
        unifyfs_key_t* key = &keys[count];
        key->gfid   = gfid;
        key->offset = next->start;

        /* fill in value */
        unifyfs_val_t* val = &vals[count];
        val->addr           = next->pos;
        val->len            = next->end - next->start + 1;
        val->delegator_rank = next->svr_rank;
        val->app_id         = next->app_id;
        val->rank           = next->cli_id;

        /* increment the number of key/values we found */
        count++;

        /* get the next element in the tree */
        next = extent_tree_iter(extent_tree, next);
    }

    /* return to user the number of key/values we set */
    *outnum = count;

    /* done reading the tree */
    extent_tree_unlock(extent_tree);

    return 0;
}

static void chunk_req_from_extent(
    unsigned long req_offset,
    unsigned long req_len,
    struct extent_tree_node* n,
    chunk_read_req_t* chunk)
{
    unsigned long offset = n->start;
    unsigned long nbytes = n->end - n->start + 1;
    unsigned long log_offset = n->pos;
    unsigned long last = req_offset + req_len - 1;

    if (offset < req_offset) {
        unsigned long diff = req_offset - offset;

        offset = req_offset;
        log_offset += diff;
        nbytes -= diff;
    }

    if (n->end > last) {
        unsigned long diff = n->end - last;
        nbytes -= diff;
    }

    chunk->offset = offset;
    chunk->nbytes = nbytes;
    chunk->log_offset = log_offset;
    chunk->rank = n->svr_rank;
    chunk->log_client_id = n->cli_id;
    chunk->log_app_id = n->app_id;
}

int extent_tree_get_chunk_list(
    struct extent_tree* extent_tree, /* extent tree to search */
    unsigned long offset,            /* starting logical offset */
    unsigned long len,               /* ending logical offset */
    unsigned int* n_chunks,          /* [out] number of extents returned */
    chunk_read_req_t** chunks)       /* [out] extent array */
{
    int ret = 0;
    unsigned int count = 0;
    unsigned long end = offset + len - 1;
    struct extent_tree_node* first = NULL;
    struct extent_tree_node* next = NULL;
    chunk_read_req_t* out_chunks = NULL;
    chunk_read_req_t* current = NULL;

    extent_tree_rdlock(extent_tree);

    first = extent_tree_find(extent_tree, offset, end);
    next = first;
    while (next && next->start <= end) {
        count++;
        next = extent_tree_iter(extent_tree, next);
    }

    *n_chunks = count;
    if (0 == count) {
        goto out_unlock;
    }

    out_chunks = calloc(count, sizeof(*out_chunks));
    if (!out_chunks) {
        ret = ENOMEM;
        goto out_unlock;
    }

    next = first;
    current = out_chunks;
    while (next && next->start <= end) {
        /* trim out the extent so it does not include the data that is not
         * requested */
        chunk_req_from_extent(offset, len, next, current);

        next = extent_tree_iter(extent_tree, next);
        current += 1;
    }

    *chunks = out_chunks;

out_unlock:
    extent_tree_unlock(extent_tree);

    return ret;
}

