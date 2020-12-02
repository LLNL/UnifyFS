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
 * Test file size functions
 *
 * Test description:
 * 1. For each rank, write 1KB at a 1KB * rank offset.
 * 2. Check the file size.  It should be 0 since it hasn't been laminated.
 * 3. Have the last rank laminate the file.
 * 4. Check the file size again.  It should be the real, laminated, file size.
 */

#include "testutil.h"

int do_test(test_cfg* cfg)
{
    char* file;
    int rank = cfg->rank;

    file = mktemp_cmd(cfg, "/unifyfs");
    if (NULL == file) {
        return ENOMEM;
    }

    test_print(cfg, "I'm writing 1 KiB to %s at my offset at %ld",
               file, rank * 1024);
    dd_cmd(cfg, "/dev/zero", file, 1024, 1, rank);

    test_print(cfg, "Stating the file");
    stat_cmd(cfg, file);

    test_print(cfg, "After writing, file size is %lu, apparent-size %lu",
               du_cmd(cfg, file, 0), du_cmd(cfg, file, 1));

    /* sync our extents */
    sync_cmd(cfg, file);

    /* Wait for everyone to finish writing */
    test_barrier(cfg);

    if (rank == cfg->n_ranks - 1) {
        test_print(cfg, "I'm the last rank, so I'll laminate the file");

        /* laminate by removing write bits */
        chmod(file, 0444);  /* set to read-only */
        test_print(cfg, "After lamination, file size is %lu",
                   du_cmd(cfg, file, 0));

        test_print(cfg, "Stating the file");
        stat_cmd(cfg, file);
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
