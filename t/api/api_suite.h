/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/* This is the collection of library API tests.
 *
 * When new API functionality needs to be tested:
 * 1. Create a t/api/<function>.c file with a function called:
 *       api_<function>_test(char *unifyfs_root)
 *    to contain all the TAP tests for that API functionality.
 * 2. Add the function name to this file, with comments.
 * 3. In t/Makefile.am, add the new file to the source file list for
 *    the api test suite (api_client_api_test_t_SOURCES).
 * 4. The api_<function>_test function can now be called from the suite's
 *    implementation in t/api/api_suite.c */

#ifndef T_LIBRARY_API_SUITE_H
#define T_LIBRARY_API_SUITE_H

#include "t/lib/tap.h"
#include "t/lib/testutil.h"
#include "unifyfs_api.h"

/* Tests API initialization */
int api_initialize_test(char* unifyfs_root,
                        unifyfs_handle* fshdl);

/* Tests API get-configuration */
int api_config_test(char* unifyfs_root,
                    unifyfs_handle* fshdl);

/* Tests API finalization */
int api_finalize_test(char* unifyfs_root,
                      unifyfs_handle* fshdl);

/* Tests file creation, open, and removal */
int api_create_open_remove_test(char* unifyfs_root,
                                unifyfs_handle* fshdl);

/* Tests file write, read, sync, and stat */
int api_write_read_sync_stat_test(char* unifyfs_root,
                                  unifyfs_handle* fshdl,
                                  size_t filesize,
                                  size_t chksize);

/* Tests the get_gfid_list and get_server_file_metadata APIs */
int api_get_gfids_and_metadata_test(char* unifyfs_root,
                                    unifyfs_handle* fshdl,
                                    size_t filesize);

/* Tests file laminate, with subsequent write/read/stat */
int api_laminate_test(char* unifyfs_root,
                      unifyfs_handle* fshdl);


/* Tests file storage space reuse */
int api_storage_test(char* unifyfs_root,
                     unifyfs_handle* fshdl,
                     size_t filesize,
                     size_t chksize);


/* Tests file transfers, both serial and parallel */
int api_transfer_test(char* unifyfs_root,
                      char* tmpdir,
                      unifyfs_handle* fshdl,
                      size_t filesize,
                      size_t chksize);

#endif /* T_LIBRARY_API_SUITE_H */
