/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */


/* This is the collection of metadata tests to be run inside of
 * metadata_suite.c. These tests are testing the wrapper functions found in
 * server/src/unifycr_metadata.c.
 */


#ifndef METADATA_SUITE_H
#define METADATA_SUITE_H

int unifycr_set_file_attribute_test(void);
int unifycr_get_file_attribute_test(void);
int unifycr_get_file_extents_test(void);

#endif /* METADATA_SUITE_H */
