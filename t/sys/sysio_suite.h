/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */


/* This is the collection of sysio wrapper tests to be run inside of
 * sysio_suite.c. These tests are testing the wrapper functions found in
 * client/src/unifycr-sysio.c.
 *
 *
 * When a new wrapper in unifycr-sysio.c needs to be tested:
 * 1. Create a <sysio_function_name>.c file with a function called
 *    <sysio_function_name>_test(char *unifycr_root) that contains all the TAP
 *    tests for that wrapper.
 * 2. Add the function name to this file, with comments.
 * 3. Add the <sysio_function_name>.c file to the /t/Makefile.am under the
 *    appropriate test suite at the bottom.
 * 4. The <sysio_function_name>_test function can now be called from
 *    sysio_suite.c. */
#ifndef SYSIO_SUITE_H
#define SYSIO_SUITE_H

/* Tests for UNIFYCR_WRAP(creat) and UNIFYCR_WRAP(close) */
int creat_close_test(char* unifycr_root);

/* Tests for UNIFYCR_WRAP(open) */
int open_test(char* unifycr_root);

#endif /* SYSIO_SUITE_H */
