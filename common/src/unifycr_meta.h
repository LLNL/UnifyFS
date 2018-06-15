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

#ifndef UNIFYCR_META_H
#define UNIFYCR_META_H

#include <sys/stat.h>

#include "unifycr_const.h"

typedef struct {
    int fid;
    int gfid;
    char filename[UNIFYCR_MAX_FILENAME];
    struct stat file_attr;
} unifycr_file_attr_t;

typedef struct {
    off_t file_pos;
    off_t mem_pos;
    size_t length;
    int fid;
} unifycr_index_t;

#endif /* UNIFYCR_META_H */
