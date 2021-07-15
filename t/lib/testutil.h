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

#include <stdint.h>

/*
 * Store a random string of length @len into buffer @buf.
 */
void testutil_rand_string(char* buf, size_t len);

/*
 * Generate a path of length @len and store it into buffer @buf. The
 * path will begin with the NUL-terminated string pointed to by @pfx,
 * followed by a slash (/), followed by a random sequence of characters.
 */
void testutil_rand_path(char* buf, size_t len, const char* pfx);

/*
 * Return a pointer to the path name of the test temp directory. Use the
 * value of the environment variable UNIFYFS_TEST_TMPDIR if it exists,
 * otherwise use P_tmpdir (defined in stdio.h, typically '/tmp').
 */
char* testutil_get_tmp_dir(void);

/*
 * Return a pointer to the path name of the UnifyFS mount point. Use the
 * value of the environment variable UNIFYFS_MOUNTPOINT if it exists,
 * otherwise use 'tmpdir/unifyfs'.
 */
char* testutil_get_mount_point(void);

/* Stat the file associated to by path and store the global size of the
 * file at path in the address of the global pointer passed in. */
void testutil_get_size(char* path, size_t* global);

/* Sequentially number every 8 bytes (uint64_t) in given buffer */
void testutil_lipsum_generate(char* buf, uint64_t len, uint64_t offset);

/*
 * Check buffer contains lipsum generated data.
 * returns 0 on success, -1 otherwise with @error_offset set.
 */
int testutil_lipsum_check(const char* buf, uint64_t len, uint64_t offset,
                          uint64_t* error_offset);


/*
 * Check buffer contains all zero bytes.
 * returns 0 on success, -1 otherwise.
 */
int testutil_zero_check(const char* buf, size_t len);
