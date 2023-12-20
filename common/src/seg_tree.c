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

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "seg_tree.h"
#include "tree.h"
#include "unifyfs_log.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static int
stn_compare_func(struct seg_tree_node* node1,
                 struct seg_tree_node* node2)
{
    if (node1->start > node2->end) {
        return 1;
    } else if (node1->end < node2->start) {
        return -1;
    } else {
        return 0;
    }
}

RB_PROTOTYPE(inttree, seg_tree_node, entry, stn_compare_func)
RB_GENERATE(inttree, seg_tree_node, entry, stn_compare_func)

/* Returns 0 on success, positive non-zero error code otherwise */
int seg_tree_init(struct seg_tree* seg_tree)
{
    memset(seg_tree, 0, sizeof(*seg_tree));
    ABT_rwlock_create(&(seg_tree->rwlock));
    RB_INIT(&seg_tree->head);

    return 0;
}

/*
 * Remove and free all nodes in the seg_tree.
 */
void seg_tree_destroy(struct seg_tree* seg_tree)
{
    seg_tree_clear(seg_tree);
    ABT_rwlock_free(&(seg_tree->rwlock));
}

/* Allocate a node for the range tree.  Free node with free() when finished */
static struct seg_tree_node*
seg_tree_node_alloc(unsigned long start,
                    unsigned long end,
                    unsigned long ptr,
                    int client_id)
{
    /* allocate a new node structure */
    struct seg_tree_node* node;
    node = calloc(1, sizeof(*node));
    if (!node) {
        return NULL;
    }

    /* record logical range and physical offset */
    node->start = start;
    node->end = end;
    node->ptr = ptr;
    node->client_id = client_id;

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
    /*
     * This function is only called when we know that segment 1 and segment 2
     * overlap with each other. Find first portion of segment 1 that does not
     * overlap with segment 2, if any.
     */
    if (start1 < start2) {
        /*
         * Segment 1 includes a portion before segment 2 starts return start/end
         * of that leading portion of segment 1.
         *
         * s1-------e1
         *     s2--------e2
         *   ---- non-overlap
         */
        *new_start = start1;
        *new_end = start2 - 1;
        return 0;
    } else if (end1 > end2) {
        /*
         * Segment 1 does not start before segment 2, but segment 1 extends past
         * end of segment 2. return start/end of trailing portion of segment 1.
         *
         *       s1-----e1
         *  s2-------e2
         *           --- non-overlap
         */
        *new_start = end2 + 1;
        *new_end = end1;
        return 0;
    }

    /*
     * Segment 2 completely envelops segment 1 so nothing left of segment 1 to
     * return, so return 1 to indicate this case.
     *
     *    s1-------e1
     * s2-------------e2
     */
    return 1;
}

/*
 * Add an entry to the range tree.  Returns 0 on success, nonzero otherwise.
 */
int seg_tree_add(struct seg_tree* seg_tree, unsigned long start,
    unsigned long end, unsigned long ptr, int client_id)
{
    /* Assume we'll succeed */
    int rc = 0;
    struct seg_tree_node* node;
    struct seg_tree_node* remaining;
    struct seg_tree_node* resized;
    struct seg_tree_node* overlap;
    struct seg_tree_node* target;
    struct seg_tree_node* prev;
    struct seg_tree_node* next;
    long new_start;
    long new_end;
    unsigned long ptr_end;
    int ret;

    /* Create our range */
    node = seg_tree_node_alloc(start, end, ptr, client_id);
    if (!node) {
        return ENOMEM;
    }

    /* Lock the tree so we can modify it */
    seg_tree_wrlock(seg_tree);

    /*
     * Try to insert our range into the RB tree.  If it overlaps with any other
     * range, then it is not inserted, and the overlapping range node is
     * returned in 'overlap'.  If 'overlap' is NULL, then there were no
     * overlaps, and our range was successfully inserted.
     */
    overlap = NULL;
    while ((overlap = RB_INSERT(inttree, &seg_tree->head, node))) {
        /*
         * Our range overlaps with another range (in 'overlap'). Is there any
         * any part of 'overlap' that does not overlap our range?  If so,
         * delete the old 'overlap' and insert the smaller, non-overlapping
         * range.
         */
        ret = get_non_overlapping_range(overlap->start, overlap->end, start,
            end, &new_start, &new_end);
        if (ret) {
            /*
             * The new range we are adding completely covers the existing
             * range in the tree defined in overlap. We can't find a
             * non-overlapping range.  Delete the existing range.
             */
            RB_REMOVE(inttree, &seg_tree->head, overlap);
            free(overlap);
            seg_tree->count--;
        } else {
            /*
             * Part of the old range was non-overlapping.  Split the old range
             * into two ranges: one for the non-overlapping section, and one for
             * the remaining section.  The non-overlapping section gets
             * inserted without issue.  The remaining section will be processed
             * on the next pass of this while() loop.
             */
            resized = seg_tree_node_alloc(new_start, new_end,
                overlap->ptr + (new_start - overlap->start), client_id);
            if (!resized) {
                free(node);
                rc = ENOMEM;
                goto release_add;
            }

            /*
             * If the non-overlapping part came from the front portion of the
             * existing range, then there is a trailing portion of the
             * existing range to add back to be considered again in the next
             * loop iteration.
             */
            remaining = NULL;
            if (resized->end < overlap->end) {
                /*
                 * There's still a remaining section after the non-overlapping
                 * part.  Add it in.
                 */
                remaining = seg_tree_node_alloc(
                    resized->end + 1,
                    overlap->end,
                    overlap->ptr + (resized->end + 1 - overlap->start),
                    client_id);
                if (!remaining) {
                    free(node);
                    free(resized);
                    rc = ENOMEM;
                    goto release_add;
                }
            }

            /* Remove our old range */
            RB_REMOVE(inttree, &seg_tree->head, overlap);
            free(overlap);
            seg_tree->count--;

            /* Insert the non-overlapping part of the new range */
            RB_INSERT(inttree, &seg_tree->head, resized);
            seg_tree->count++;

            /*
             * If we have a trailing portion, insert range for that, and
             * increase our extent count since we just turned one range entry
             * into two
             */
            if (remaining != NULL) {
                RB_INSERT(inttree, &seg_tree->head, remaining);
                seg_tree->count++;
            }
        }
    }

    /* Increment segment count in the tree for the range we just added */
    seg_tree->count++;

    /*
     * Update max ending offset if end of new range we just inserted
     * is larger.
     */
    seg_tree->max = MAX(seg_tree->max, end);

    /* Get temporary pointer to the node we just added. */
    target = node;

    /* Check whether we can coalesce new extent with any preceding extent. */
    prev = RB_PREV(inttree, &seg_tree->head, target);
    if ((prev != NULL) && ((prev->end + 1) == target->start)) {
        /*
         * We found a extent that ends just before the new extent starts.
         * Check whether they are also contiguous in the log.
         */
        ptr_end = prev->ptr + (prev->end - prev->start + 1);
        if (ptr_end == target->ptr) {
            /*
             * The preceding extent describes a log position adjacent to
             * the extent we just added, so we can merge them.
             * Append entry to previous by extending end of previous.
             */
            prev->end = target->end;

            /* Delete new extent from the tree and free it. */
            RB_REMOVE(inttree, &seg_tree->head, target);
            free(target);
            seg_tree->count--;

            /*
             * Update target to point at previous extent since we just
             * merged our new extent into it.
             */
            target = prev;
        }
    }

    /* Check whether we can coalesce new extent with any trailing extent. */
    next = RB_NEXT(inttree, &seg_tree->head, target);
    if ((next != NULL) && ((target->end + 1) == next->start)) {
        /*
         * We found a extent that starts just after the new extent ends.
         * Check whether they are also contiguous in the log.
         */
        ptr_end = target->ptr + (target->end - target->start + 1);
        if (ptr_end == next->ptr) {
            /*
             * The target extent describes a log position adjacent to
             * the next extent, so we can merge them.
             * Append entry to target by extending end of to cover next.
             */
            target->end = next->end;

            /* Delete next extent from the tree and free it. */
            RB_REMOVE(inttree, &seg_tree->head, next);
            free(next);
            seg_tree->count--;
        }
    }

release_add:

    seg_tree_unlock(seg_tree);

    return rc;
}

/*
 * Remove or truncate one or more entries from the range tree
 * if they overlap [start, end].
 *
 * Returns 0 on success, nonzero otherwise.
 */
int seg_tree_remove(
    struct seg_tree* seg_tree,
    unsigned long start,
    unsigned long end)
{
    struct seg_tree_node* node;

    LOGDBG("removing extents overlapping [%lu, %lu]", start, end);

    seg_tree_wrlock(seg_tree);
    node = seg_tree_find_nolock(seg_tree, start, end);
    while (node != NULL) {
        if (start <= node->start) {
            if (node->end <= end) {
                /* start <= node_s <= node_e <= end
                 * remove whole extent */
                LOGDBG("removing node [%lu, %lu]", node->start, node->end);
                RB_REMOVE(inttree, &seg_tree->head, node);
                free(node);
                seg_tree->count--;
            } else {
                /* start <= node_s <= end < node_e
                 * update node start */
                LOGDBG("updating node start from %lu to %lu",
                       node->start, (end + 1));

                node->ptr += (end + 1 - node->start);
                node->start = end + 1;
            }
        } else if (node->start < start) {
            if (node->end <= end) {
                /* node_s < start <= node_e <= end
                 * truncate node */
                LOGDBG("updating node end from %lu to %lu",
                       node->end, (start - 1));
                node->end = start - 1;
            } else {
                /* node_s < start <= end < node_e
                 * extent spans entire region, split into two nodes
                 * representing before/after region */
                unsigned long a_end = node->end;
                unsigned long a_start = end + 1;
                unsigned long a_ptr = node->ptr + (a_start - node->start);

                /* truncate existing (before) node */
                LOGDBG("updating before node end from %lu to %lu",
                       node->end, (start - 1));
                node->end = start - 1;

                /* add new (after) node */
                LOGDBG("add after node [%lu, %lu]", a_start, a_end);
                seg_tree_unlock(seg_tree);
                int rc = seg_tree_add(seg_tree,
                                      a_start,
                                      a_end,
                                      a_ptr,
                                      node->client_id);
                if (rc) {
                    LOGERR("seg_tree_add() failed when splitting");
                    return rc;
                }
                seg_tree_wrlock(seg_tree);
            }
        }
        /* keep looking for nodes that overlap target region */
        node = seg_tree_find_nolock(seg_tree, start, end);
    }
    seg_tree_unlock(seg_tree);

    return 0;
}

/*
 * Search tree for an entry that overlaps with given range of [start, end].
 * Returns the first overlapping entry if found, which is the overlapping entry
 * having the lowest starting offset, and returns NULL otherwise.
 *
 * This function assumes you've already locked the seg_tree.
 */
struct seg_tree_node* seg_tree_find_nolock(
    struct seg_tree* seg_tree,
    unsigned long start,
    unsigned long end)
{
    /* Create a range of just our starting byte offset */
    struct seg_tree_node* node = seg_tree_node_alloc(start, start, 0, 0);
    if (!node) {
        return NULL;
    }

    /* Search tree for either a range that overlaps with
     * the target range (starting byte), or otherwise the
     * node for the next biggest starting byte. */
    struct seg_tree_node* next = RB_NFIND(inttree, &seg_tree->head, node);

    free(node);

    /* We may have found a node that doesn't include our starting
     * byte offset, but it would be the range with the lowest
     * starting offset after the target starting offset.  Check whether
     * this overlaps our end offset */
    if (next && next->start <= end) {
        return next;
    }

    /* Otherwise, there is not element that overlaps with the
     * target range of [start, end]. */
    return NULL;
}

/*
 * Search tree for an entry that overlaps with given range of [start, end].
 * Returns the first overlapping entry if found, which is the overlapping entry
 * having the lowest starting offset, and returns NULL otherwise.
 */
struct seg_tree_node* seg_tree_find(
    struct seg_tree* seg_tree,
    unsigned long start,
    unsigned long end)
{
    struct seg_tree_node* node;

    seg_tree_rdlock(seg_tree);
    node = seg_tree_find_nolock(seg_tree, start, end);
    seg_tree_unlock(seg_tree);

    return node;
}

/*
 * Given a range tree and a starting node, iterate though all the nodes
 * in the tree, returning the next one each time.  If start is NULL, then
 * start with the first node in the tree.
 *
 * This is meant to be called in a loop, like:
 *
 *    seg_tree_rdlock(seg_tree);
 *
 *    struct seg_tree_node *node = NULL;
 *    while ((node = seg_tree_iter(seg_tree, node))) {
 *       printf("[%d-%d]", node->start, node->end);
 *    }
 *
 *    seg_tree_unlock(seg_tree);
 *
 * Note: this function does no locking, and assumes you're properly locking
 * and unlocking the seg_tree before doing the iteration (see
 * seg_tree_rdlock()/seg_tree_wrlock()/seg_tree_unlock()).
 */
struct seg_tree_node*
seg_tree_iter(struct seg_tree* seg_tree, struct seg_tree_node* start)
{
    struct seg_tree_node* next = NULL;
    struct seg_tree_node* tmp = NULL;
    if (start == NULL) {
        /* Initial case, no starting node */
        next = RB_MIN(inttree, &seg_tree->head);
        return next;
    }

    /*
     * We were given a valid start node.  Look it up to start our traversal
     * from there.
     */
    tmp = RB_FIND(inttree, &seg_tree->head, start);
    if (!tmp) {
        /* Some kind of error */
        return NULL;
    }

    /* Look up our next node */
    next = RB_NEXT(inttree, &seg_tree->head, start);

    return next;
}

/*
 * Lock a seg_tree for reading.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void
seg_tree_rdlock(struct seg_tree* seg_tree)
{
    int rc = ABT_rwlock_rdlock(seg_tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_rdlock() failed - rc=%d", rc);
    }
}

/*
 * Lock a seg_tree for read/write.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void
seg_tree_wrlock(struct seg_tree* seg_tree)
{
    int rc = ABT_rwlock_wrlock(seg_tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_wrlock() failed - rc=%d", rc);
    }
}

/*
 * Unlock a seg_tree for read/write.  This should only be used for calling
 * seg_tree_iter().  All the other seg_tree functions provide their
 * own locking.
 */
void
seg_tree_unlock(struct seg_tree* seg_tree)
{
    int rc = ABT_rwlock_unlock(seg_tree->rwlock);
    if (rc) {
        LOGERR("ABT_rwlock_unlock() failed - rc=%d", rc);
    }
}

/*
 * Remove all nodes in seg_tree, but keep it initialized so you can
 * seg_tree_add() to it.
 */
void seg_tree_clear(struct seg_tree* seg_tree)
{
    struct seg_tree_node* node = NULL;
    struct seg_tree_node* oldnode = NULL;

    seg_tree_wrlock(seg_tree);

    if (RB_EMPTY(&seg_tree->head)) {
        /* seg_tree is empty, nothing to do */
        seg_tree_unlock(seg_tree);
        return;
    }

    /* Remove and free each node in the tree */
    while ((node = seg_tree_iter(seg_tree, node))) {
        if (oldnode) {
            RB_REMOVE(inttree, &seg_tree->head, oldnode);
            free(oldnode);
        }
        oldnode = node;
    }
    if (oldnode) {
        RB_REMOVE(inttree, &seg_tree->head, oldnode);
        free(oldnode);
    }

    seg_tree->count = 0;
    seg_tree->max = 0;

    seg_tree_unlock(seg_tree);
}

/* Return the number of segments in the segment tree */
unsigned long seg_tree_count(struct seg_tree* seg_tree)
{
    seg_tree_rdlock(seg_tree);
    unsigned long count = seg_tree->count;
    seg_tree_unlock(seg_tree);
    return count;
}

/* Return the maximum ending logical offset in the tree */
unsigned long seg_tree_max(struct seg_tree* seg_tree)
{
    seg_tree_rdlock(seg_tree);
    unsigned long max = seg_tree->max;
    seg_tree_unlock(seg_tree);
    return max;
}
