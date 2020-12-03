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

/*
 * Test chmod() and fchmod()
 *
 * Test description:
 * 1. Make an empty file.
 * 2. Try setting all the permission bits
 * 3. Verify you see the bits in stat()
 * 4. Use chmod() to remove the write bits to laminate it
 * 5. Check file is laminated after
 * 6. Make another empty file
 * 7. Laminate it using fchmod()
 * 8. Check file is laminated after
 */

#include "testutil.h"

int do_test(test_cfg* cfg)
{
    char* file;
    int rank = cfg->rank;
    int fd;

    file = mktemp_cmd(cfg, "/unifyfs");
    if (NULL == file) {
        return ENOMEM;
    }

    test_print(cfg, "Create empty file %s", file);
    test_print(cfg, "Before lamination stat() is:");
    stat_cmd(cfg, file);

    test_print(cfg, "Try setting all the bits.  stat() is:");
    chmod(file, 0777);
    stat_cmd(cfg, file);

    if (rank == cfg->n_ranks - 1) {
        test_print(cfg, "I'm the last rank, so I'll laminate the file");

        sync_cmd(cfg, file);
        /* laminate by setting permissions to read-only */
        chmod(file, 0444);

        test_print(cfg, "After lamination stat() is:");
        stat_cmd(cfg, file);
    }
    free(file);

    /* Do the same thing as before, but use fchmod() */
    file = mktemp_cmd(cfg, "/unifyfs");
    test_print(cfg, "Create empty file %s", file);
    test_print(cfg, "Before lamination stat() is:");
    stat_cmd(cfg, file);
    if (rank == cfg->n_ranks - 1) {
        test_print(cfg, "I'm the last rank, so I'll fchmod laminate the file");

        fd = open(file, O_RDWR);
        /* laminate by removing write bits */
        fchmod(fd, 0444);   /* set to read-only */
        close(fd);

        test_print(cfg, "After lamination stat() is:");
        stat_cmd(cfg, file);
        test_print(cfg, "Verifying that writes to the laminated file fail");
        dd_cmd(cfg, "/dev/zero", file, 1024, 1, 0);
    }
    free(file);
}

int main(int argc, char* argv[])
{
    test_cfg test_config;
    test_cfg* cfg = &test_config;
    int rc;

    rc = test_init(argc, argv, cfg);
    if (rc) {
        test_print(cfg, "ERROR - Test %s initialization failed!", argv[0]);
        fflush(NULL);
        return rc;
    }

    rc = do_test(cfg);
    if (rc) {
        test_print(cfg, "ERROR - Test %s failed! rc=%d", argv[0], rc);
        fflush(NULL);
    }

    test_fini(cfg);

    return rc;
}
