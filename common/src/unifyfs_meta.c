/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <endian.h>
#include <string.h>
#include <openssl/md5.h>

#include "unifyfs_meta.h"

/* extent slice size used for metadata */
size_t meta_slice_sz = META_DEFAULT_RANGE_SZ;

/* calculate number of slices in an extent given by start offset and length */
size_t meta_num_slices(size_t offset, size_t length)
{
    size_t start = offset / meta_slice_sz;
    size_t end   = (offset + length - 1) / meta_slice_sz;
    size_t count = end - start + 1;
    return count;
}

/**
 * Hash a file path to a 64-bit unsigned integer using MD5
 * @param path absolute file path
 * @return hash value
 */
uint64_t compute_path_md5(const char* path)
{
    unsigned long len;
    unsigned char digested[16] = {0};

    len = strlen(path);
    MD5((const unsigned char*) path, len, digested);

    /* construct uint64_t hash from first 8 digest bytes */
    uint64_t hash = be64toh(*((uint64_t*)digested));
    return hash;
}
