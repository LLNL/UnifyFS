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
 * Copyright by The HDF Group.
 * Copyright by the Board of Trustees of the University of Illinois.
 * All rights reserved.
 *
 * This file is part of HDF5.  The full HDF5 copyright notice, including
 * terms governing use, modification, and redistribution, is contained in
 * the COPYING file, which can be found at the root of the source code
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.
 * If you do not have access to either file, you may request a copy from
 * help@hdfgroup.org.
 */

/*
 *  This example illustrates how to create a dataset that is a 4 x 6
 *  array.  It is used in the HDF5 Tutorial.
 */
/*
 * The example is modified to test unifyfs userspace file system.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <mpi.h>
#include <unifyfs.h>
#include <hdf5.h>

#include "testlib.h"

static int rank;
static int total_ranks;
static int write_data;

static int debug;           /* pause for attaching debugger */
static int standard;        /* not mounting unifyfs when set */
static int unmount;         /* unmount unifyfs after running the test */
static char* mountpoint = "/unifyfs";   /* unifyfs mountpoint */
static char* filename = "test.h5";  /* testfile name under mountpoint */
static char targetfile[NAME_MAX];   /* target file name */

static struct option const long_opts[] = {
    { "debug", 0, 0, 'd' },
    { "filename", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "standard", 0, 0, 's' },
    { "unmount", 0, 0, 'u' },
    { "write", 0, 0, 'w' },
    { 0, 0, 0, 0},
};

static char* short_opts = "df:hm:suw";

static const char* usage_str =
"\n"
"Usage: %s [options...]\n"
"\n"
"Available options:\n"
" -d, --debug                      pause before running test\n"
"                                  (handy for attaching in debugger)\n"
" -f, --filename=<filename>        target file name under mountpoint\n"
"                                  (default: test.h5)\n"
" -h, --help                       help message\n"
" -m, --mount=<mountpoint>         use <mountpoint> for unifyfs\n"
"                                  (default: /unifyfs)\n"
" -s, --standard                   do not use unifyfs but run standard I/O\n"
" -u, --unmount                    unmount the filesystem after test\n"
" -w, --write                      populate the dataset\n"
"\n";

static char* program;

static void print_usage(void)
{
    test_print_once(rank, usage_str, program);
    exit(0);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int ch = 0;
    int optidx = 2;

    hid_t file_id, dataset_id, dataspace_id;  /* identifiers */
    hsize_t dims[2];
    herr_t status;

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'd':
            debug = 1;
            break;

        case 'f':
            filename = strdup(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 's':
            standard = 1;
            break;

        case 'u':
            unmount = 1;
            break;

        case 'w':
            write_data = 1;
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    sprintf(targetfile, "%s/%s", mountpoint, filename);

    if (debug)
        test_pause(rank, "Attempting to mount");

    if (!standard) {
        ret = unifyfs_mount(mountpoint, rank, total_ranks);
        if (ret) {
            test_print(rank, "unifyfs_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    if (rank == 0) {
        /* Create a new file using default properties. */
        file_id = H5Fcreate(targetfile, H5F_ACC_TRUNC, H5P_DEFAULT,
                            H5P_DEFAULT);
        printf("H5Fcreate: %ld\n", (long) file_id);

        /* Create the data space for the dataset. */
        dims[0] = 4;
        dims[1] = 6;
        dataspace_id = H5Screate_simple(2, dims, NULL);
        printf("H5Screate_simple: %ld\n", (long) dataspace_id);

        /* Create the dataset. */
        dataset_id = H5Dcreate2(file_id, "/dset", H5T_STD_I32BE, dataspace_id,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        printf("H5Dcreate2: %ld\n", (long) dataset_id);

        if (write_data) {
            int i, j;
            int dset_data[4][6];

            for (i = 0; i < 4; i++)
                for (j = 0; j < 6; j++)
                    dset_data[i][j] = i * 6 + j + 1;

            /* Write the dataset. */
            status = H5Dwrite(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL,
                              H5P_DEFAULT, dset_data);
            printf("H5Dwrite: %d\n", status);
        }

        /* End access to the dataset and release resources used by it. */
        status = H5Dclose(dataset_id);
        printf("H5Dclose: %d\n", status);

        /* Terminate access to the data space. */
        status = H5Sclose(dataspace_id);
        printf("H5Sclose: %d\n", status);

        /* Close the file. */
        status = H5Fclose(file_id);
        printf("H5Fclose: %d\n", status);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (!standard && unmount) {
        unifyfs_unmount();
    }

    return 0;
}
