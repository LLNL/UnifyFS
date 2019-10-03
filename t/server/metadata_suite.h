/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */


/* This is the collection of metadata tests to be run inside of
 * metadata_suite.c. These tests are testing the wrapper functions found in
 * server/src/unifyfs_metadata.c.
 */


#ifndef METADATA_SUITE_H
#define METADATA_SUITE_H

int unifyfs_set_file_attribute_test(void);
int unifyfs_get_file_attribute_test(void);
int unifyfs_get_file_extents_test(void);

#endif /* METADATA_SUITE_H */
