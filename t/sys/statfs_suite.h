/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
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


/* This test checks that statfs returns the correct value when one
 * has set UNIFYFS_CLIENT_SUPER_MAGIC=0.  It runs as a separate
 * test suite because it requires a different environmental
 * configuration than the other tests. */
#ifndef STATFS_SUITE_H
#define STATFS_SUITE_H

/* Tests for UNIFYFS_WRAP(statfs) */
int statfs_test(char* unifyfs_root, int expect_unifyfs_magic);

#endif /* STATFS_SUITE_H */
