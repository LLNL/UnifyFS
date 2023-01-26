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


/* This is the collection of stdio wrapper tests to be run inside of
 * stdio_suite.c. These tests are testing the wrapper functions found in
 * client/src/unifyfs-stdio.c.
 *
 *
 * When a new wrapper in unifyfs-stdio.c needs to be tested:
 * 1. Create a <stdio_function_name>.c file with a function called
 *    <stdio_function_name>_test(char* unifyfs_root) that contains all the TAP
 *    tests for that wrapper.
 * 2. Add the function name to this file, with comments.
 * 3. Add the <stdio_function_name>.c file to the /t/Makefile.am under the
 *    appropriate test suite at the bottom.
 * 4. The <stdio_function_name>_test function can now be called from
 *    stdio_suite.c. */
#ifndef STDIO_SUITE_H
#define STDIO_SUITE_H

/* Tests for UNIFYFS_WRAP(fopen) and UNIFYFS_WRAP(fclose) */
int fopen_fclose_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(fseek/ftell/rewind) */
int fseek_ftell_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(fwrite) and UNIFYFS_WRAP(fread) */
int fwrite_fread_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(fflush) */
int fflush_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(size) */
int size_test(char* unifyfs_root);

/* Tests for UNIFYFS_WRAP(fopen) truncate */
int truncate_on_open(char* unifyfs_root);

#endif /* STDIO_SUITE_H */
