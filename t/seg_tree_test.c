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
        ptr += sprintf(&dst[ptr], "[%lu-%lu:%c]", node->start, node->end,
            *((char*)node->ptr));
    }
    seg_tree_unlock(seg_tree);
    return dst;
}

int main(int argc, char** argv)
{
    struct seg_tree seg_tree;
    char buf[500];
    char tmp[255];
    int i;
    unsigned long max, count;
    struct seg_tree_node* node;

    plan(NO_PLAN);

    /*
     * Initialize our buffer with the character for the buffer pos (0-9).
     * We'll use this to make sure the node.ptr value is getting updated
     * correctly by the seg_tree.
     */
    for (i = 0; i < sizeof(buf); i++) {
        buf[i] = (i % 10) + '0';
    }

    seg_tree_init(&seg_tree);

    /* Initial insert */
    seg_tree_add(&seg_tree, 5, 10, (unsigned long) (buf + 5));
    is("[5-10:5]", print_tree(tmp, &seg_tree), "Initial insert works");

    /* Non-overlapping insert */
    seg_tree_add(&seg_tree, 100, 150, (unsigned long) (buf + 100));
    is("[5-10:5][100-150:0]", print_tree(tmp, &seg_tree),
        "Non-overlapping works");

    /* Add range overlapping part of the left size */
    seg_tree_add(&seg_tree, 2, 7, (unsigned long) (buf + 2));
    is("[2-7:2][8-10:8][100-150:0]", print_tree(tmp, &seg_tree),
        "Left size overlap works");

    /* Add range overlapping part of the right size */
    seg_tree_add(&seg_tree, 9, 12, (unsigned long) (buf + 9));
    is("[2-7:2][8-8:8][9-12:9][100-150:0]", print_tree(tmp, &seg_tree),
        "Right size overlap works");

    /* Add range totally within another range */
    seg_tree_add(&seg_tree, 3, 4, (unsigned long) (buf + 3));
    is("[2-2:2][3-4:3][5-7:5][8-8:8][9-12:9][100-150:0]",
        print_tree(tmp, &seg_tree), "Inside range works");

    /* Test counts */
    max = seg_tree_max(&seg_tree);
    count = seg_tree_count(&seg_tree);
    ok(max == 150, "max is 150 (got %lu)", max);
    ok(count == 6, "count is 6 (got %lu)", count);

    /* Add a range that blows away multiple ranges, and overlaps */
    seg_tree_add(&seg_tree, 4, 120, (unsigned long) (buf + 4));
    is("[2-2:2][3-3:3][4-120:4][121-150:1]", print_tree(tmp, &seg_tree),
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
    seg_tree_add(&seg_tree, 0, 50, (unsigned long) (buf + 50));
    seg_tree_add(&seg_tree, 0, 0, (unsigned long) (buf + 0));
    seg_tree_add(&seg_tree, 2, 2, (unsigned long) (buf + 2));
    seg_tree_add(&seg_tree, 4, 4, (unsigned long) (buf + 4));
    seg_tree_add(&seg_tree, 6, 6, (unsigned long) (buf + 6));
    is("[0-0:0][1-1:1][2-2:2][3-3:3][4-4:4][5-5:5][6-6:6][7-50:7]",
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
    seg_tree_add(&seg_tree, 100, 200, (unsigned long) (buf + 100));
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
    seg_tree_add(&seg_tree, 20, 30, (unsigned long) (buf + 0));
    is("[20-30:0]", print_tree(tmp, &seg_tree), "Initial [20-30] write works");

    seg_tree_add(&seg_tree, 20, 30, (unsigned long) (buf + 8));
    is("[20-30:8]", print_tree(tmp, &seg_tree), "Same range overwrite works");

    seg_tree_destroy(&seg_tree);

    done_testing();

    return 0;
}
