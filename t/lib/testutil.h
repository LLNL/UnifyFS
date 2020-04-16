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
 * Return a pointer to the path name of the UnifyFS mount point. Use the
 * value of the environment variable UNIFYFS_MOUNT_POINT if it exists,
 * otherwise use P_tmpdir which is defined in stdio.h and is typically
 * /tmp.
 */
char* testutil_get_mount_point(void);

/* Stat the file associated to by path and store the global and log sizes of the
 * file at path in the addresses of the respective global and log pointers
 * passed in.
 * User can ask for one or both sizes. */
void testutil_get_size(char* path, size_t* global, size_t* log);
