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


/* This is the collection of sysio wrapper tests to be run inside of
 * sysio_suite.c. These tests are testing the wrapper functions found in
 * client/src/unifyfs-sysio.c.
 *
 *
 * When a new wrapper in unifyfs-sysio.c needs to be tested:
 * 1. Create a <sysio_function_name>.c file with a function called
 *    <sysio_function_name>_test(char *unifyfs_root) that contains all the TAP
 *    tests for that wrapper.
 * 2. Add the function name to this file, with comments.
 * 3. Add the <sysio_function_name>.c file to the /t/Makefile.am under the
 *    appropriate test suite at the bottom.
 * 4. The <sysio_function_name>_test function can now be called from
 *    sysio_suite.c. */
#ifndef SYSIO_SUITE_H
#define SYSIO_SUITE_H

/* Tests for UNIFYFS_WRAP(statfs) */
int statfs_test(char* unifyfs_root, int expect_unifyfs_magic);

/* Tests for UNIFYFS_WRAP(creat) and UNIFYFS_WRAP(close) */
int creat_close_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(creat64) */
int creat64_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(mkdir) and UNIFYFS_WRAP(rmdir) */
int mkdir_rmdir_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(open) */
int open_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(open64) */
int open64_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(lseek) */
int lseek_test(char* unifyfs_root);

int write_read_test(char* unifyfs_root);
int write_max_read_test(char* unifyfs_root);
int write_pre_existing_file_test(char* unifyfs_root);

/* test reading from file with holes */
int write_read_hole_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(ftruncate) and UNIFYFS_WRAP(truncate) */
int truncate_test(char* unifyfs_root);
int truncate_bigempty(char* unifyfs_root);
int truncate_eof(char* unifyfs_root);
int truncate_truncsync(char* unifyfs_root);
int truncate_pattern_size(char* unifyfs_root, int pos);
int truncate_empty_read(char* unifyfs_root, int pos);
int truncate_ftrunc_before_sync(char* unifyfs_root);
int truncate_trunc_before_sync(char* unifyfs_root);
int truncate_twice(char* unifyfs_root);

/* Test for UNIFYFS_WRAP(unlink) */
int unlink_test(char* unifyfs_root);

int chdir_test(char* unifyfs_root);

/* Test for UNIFYFS_WRAP(stat, lstat, fstat) */
int stat_test(char* unifyfs_root);

#endif /* SYSIO_SUITE_H */
