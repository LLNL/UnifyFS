/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <stdio.h>
#include "seg_tree.h"
#include "t/lib/tap.h"
#include "t/lib/testutil.h"
/*
 * Test our Segment Tree library
 */

/*
 * Print the seg_tree to a buffer.  Returns dst so we can directly print the
 * result.
 */
char* print_tree(char* dst, struct seg_tree* seg_tree)
{
    int ptr = 0;
    struct seg_tree_node* node = NULL;

    /* In case we don't actually print anything */
    dst[0] = '\0';

    seg_tree_rdlock(seg_tree);
    while ((node = seg_tree_iter(seg_tree, node))) {
        ptr += sprintf(&dst[ptr], "[%lu-%lu:%lu]", node->start, node->end,
            node->ptr);
    }
    seg_tree_unlock(seg_tree);
    return dst;
}

int main(int argc, char** argv)
{
    struct seg_tree seg_tree;
    char tmp[255];
    unsigned long max, count;
    struct seg_tree_node* node;

    plan(NO_PLAN);

    seg_tree_init(&seg_tree);

    /* Initial insert */
    seg_tree_add(&seg_tree, 5, 10, 0, 0);
    is("[5-10:0]", print_tree(tmp, &seg_tree), "Initial insert works");

    /* Non-overlapping insert */
    seg_tree_add(&seg_tree, 100, 150, 100, 0);
    is("[5-10:0][100-150:100]", print_tree(tmp, &seg_tree),
        "Non-overlapping works");

    /* Add range overlapping part of the left size */
    seg_tree_add(&seg_tree, 2, 7, 200, 0);
    is("[2-7:200][8-10:3][100-150:100]", print_tree(tmp, &seg_tree),
        "Left size overlap works");

    /* Add range overlapping part of the right size */
    seg_tree_add(&seg_tree, 9, 12, 300, 0);
    is("[2-7:200][8-8:3][9-12:300][100-150:100]", print_tree(tmp, &seg_tree),
        "Right size overlap works");

    /* Add range totally within another range */
    seg_tree_add(&seg_tree, 3, 4, 400, 0);
    is("[2-2:200][3-4:400][5-7:203][8-8:3][9-12:300][100-150:100]",
        print_tree(tmp, &seg_tree), "Inside range works");

    /* Test counts */
    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 150, "max is 150 (got %lu)", max);
    ok(count == 6, "count is 6 (got %lu)", count);

    /* Add a range that blows away multiple ranges, and overlaps */
    seg_tree_add(&seg_tree, 4, 120, 500, 0);
    is("[2-2:200][3-3:400][4-120:500][121-150:121]", print_tree(tmp, &seg_tree),
        "Blow away multiple ranges works");

    /* Test counts */
    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 150, "max is 150 (got %lu)", max);
    ok(count == 4, "count is 4 (got %lu)", count);

    seg_tree_clear(&seg_tree);
    is("", print_tree(tmp, &seg_tree), "seg_tree_clear() works");

    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 0, "max 0 (got %lu)", max);
    ok(count == 0, "count is 0 (got %lu)", count);

    /*
     * Now let's write a long extent, and then sawtooth over it with 1 byte
     * extents.
     */
    seg_tree_add(&seg_tree, 0, 50, 50, 0);
    seg_tree_add(&seg_tree, 0, 0, 0, 0);
    seg_tree_add(&seg_tree, 2, 2, 2, 0);
    seg_tree_add(&seg_tree, 4, 4, 4, 0);
    seg_tree_add(&seg_tree, 6, 6, 6, 0);
    is("[0-0:0][1-1:51][2-2:2][3-3:53][4-4:4][5-5:55][6-6:6][7-50:57]",
        print_tree(tmp, &seg_tree), "Sawtooth extents works");

    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 50, "max 50 (got %lu)", max);
    ok(count == 8, "count is 8 (got %lu)", count);

    /*
     * Test seg_tree_find().  Find between a range that multiple segments.  It
     * should return the first one.
     */
    node = seg_tree_find(&seg_tree, 2, 7);
    ok(node->start == 2 && node->end == 2, "seg_tree_find found correct node");

    /* Test finding a segment that partially overlaps our range */
    seg_tree_add(&seg_tree, 100, 200, 100, 0);
    node = seg_tree_find(&seg_tree, 90, 120);
    ok(node->start == 100 && node->end == 200,
        "seg_tree_find found partial overlapping node");

    /* Look for a range that doesn't exist.  Should return NULL. */
    node = seg_tree_find(&seg_tree, 2000, 3000);
    ok(node == NULL, "seg_tree_find correctly returned NULL");

    /*
     * Write a range, then completely overwrite it with the
     * same range.  Use a different buf value to verify it changed.
     */
    seg_tree_clear(&seg_tree);
    seg_tree_add(&seg_tree, 20, 30, 0, 0);
    is("[20-30:0]", print_tree(tmp, &seg_tree), "Initial [20-30] write works");

    seg_tree_add(&seg_tree, 20, 30, 8, 0);
    is("[20-30:8]", print_tree(tmp, &seg_tree), "Same range overwrite works");

    /* Test coalescing */
    seg_tree_clear(&seg_tree);
    seg_tree_add(&seg_tree, 5, 10, 105, 0);
    is("[5-10:105]", print_tree(tmp, &seg_tree), "Initial insert works");

    /* Non-overlapping insert */
    seg_tree_add(&seg_tree, 100, 150, 200, 0);
    is("[5-10:105][100-150:200]", print_tree(tmp, &seg_tree),
        "Non-overlapping works");

    /*
     * Add range overlapping part of the left size.
     * Check that it coalesces
     */
    seg_tree_add(&seg_tree, 2, 7, 102, 0);
    is("[2-10:102][100-150:200]", print_tree(tmp, &seg_tree),
        "Left size overlap works");

    /*
     * Add range overlapping part of the right size.
     * Check that is coalesces.
     */
    seg_tree_add(&seg_tree, 9, 12, 109, 0);
    is("[2-12:102][100-150:200]", print_tree(tmp, &seg_tree),
        "Right size overlap works");

    /*
     * Add range totally within another range.
     * Check that it is consumed.
     */
    seg_tree_add(&seg_tree, 3, 4, 103, 0);
    is("[2-12:102][100-150:200]",
        print_tree(tmp, &seg_tree), "Inside range works");

    /* Test counts */
    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 150, "max is 150 (got %lu)", max);
    ok(count == 2, "count is 2 (got %lu)", count);

    /* Add a range that connects two other ranges. */
    seg_tree_add(&seg_tree, 4, 120, 104, 0);
    is("[2-150:102]", print_tree(tmp, &seg_tree),
        "Connect two ranges works");

    /* Test counts */
    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 150, "max is 150 (got %lu)", max);
    ok(count == 1, "count is 1 (got %lu)", count);

    seg_tree_clear(&seg_tree);
    seg_tree_add(&seg_tree, 0, 0, 0, 0);
    seg_tree_add(&seg_tree, 1, 10, 101, 0);
    seg_tree_add(&seg_tree, 20, 30, 20, 0);
    seg_tree_add(&seg_tree, 31, 40, 131, 0);

    /* Remove a single entry */
    seg_tree_remove(&seg_tree, 0, 0);
    ok(1 == 1, "removed a single entry, got %s", print_tree(tmp, &seg_tree));
    is("[1-10:101][20-30:20][31-40:131]", print_tree(tmp, &seg_tree),
       "removed a single range, got %s", print_tree(tmp, &seg_tree));

    /* Remove a range spanning the two bordering ranges [20-30] & [31-40]. */
    seg_tree_remove(&seg_tree, 25, 31);
    is("[1-10:101][20-24:20][32-40:132]", print_tree(tmp, &seg_tree),
       "removed a range that truncated two entries, got %s",
       print_tree(tmp, &seg_tree));

    seg_tree_clear(&seg_tree);
    seg_tree_destroy(&seg_tree);

    done_testing();

    return 0;
}
