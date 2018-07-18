/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <mpi.h>
#include <sys/time.h>
#include <errno.h>
#include <unifycr.h>

#define GEN_STR_LEN 1024

struct timeval write_start, write_end;
struct timeval read_start, read_end;
double write_time;

struct timeval write_start, write_end;
struct timeval write_start, write_end;
double read_time;

/* get BW for read or write test */
void get_bw(int *rank, int *rank_num, long *blk_sz, long *seg_num,
            struct timeval start, struct timeval end, double time)
{
    long block_size  = *blk_sz;
    long segment_num = *seg_num;

    time += 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    time = time/1000000;

    double bw = (double)block_size * segment_num / 1048576 / time;

    double agg_bw;

    MPI_Reduce(&bw, &agg_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    double max_time;

    MPI_Reduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    double min_bw;

    min_bw = (double)block_size * segment_num *
        (*rank_num) / 1048576 / max_time;

    if (*rank == 0) {
        printf("Aggregate BW is %lfMB/s, Min BW is %lfMB/s\n", agg_bw, min_bw);
        fflush(stdout);
    }
}

/* call read test */
void read_test(MPI_File *fh, MPI_Status *status,
                char *read_buf, long *blk_sz,
                long *seg_num, long *tran_sz,
                int *rank_num, int *rank,
                int *pat)
{

    long i, j, offset, rc, cursor = 0;

    for (i = 0; i < *seg_num; i++) {
        for (j = 0; j < (*blk_sz)/(*tran_sz); j++) {
            if (pat == 0)
                offset = i * (*rank_num) * (*blk_sz) + (*rank) * (*blk_sz)
                    + j * (*tran_sz);
            else if (*pat == 1)
                offset = i * (*blk_sz) + j * (*tran_sz);
            cursor += (*tran_sz);
            MPI_File_seek(*fh, offset, MPI_SEEK_SET);
            MPI_File_read(*fh, read_buf + cursor, *tran_sz, MPI_CHAR, status);
        }
    }
}

/* call write test */
void write_test(MPI_File *fh, MPI_Status *status,
                char *write_buf, long *blk_sz,
                long *seg_num, long *tran_sz,
                int *rank_num, int *rank,
                int *pat)
{

    long i, j, offset;

    for (i = 0; i < *seg_num; i++) {
        for (j = 0; j < *blk_sz / *tran_sz; j++) {
            if (pat == 0)
                offset = i * *rank_num * *blk_sz +
                    *rank * *blk_sz + j * *tran_sz;
            else if (*pat == 1)
                offset = i * *blk_sz + j * *tran_sz;
            MPI_File_seek(*fh, offset, MPI_SEEK_SET);
            MPI_File_write(*fh, write_buf, *tran_sz, MPI_CHAR, status);
        }
    }
}

int main(int argc, char *argv[])
{

    static const char *opts = "b:w:r:s:t:f:p:u:";
    char tmpfname[GEN_STR_LEN], fname[GEN_STR_LEN];
    long blk_sz, seg_num, tran_sz, num_reqs;
    int pat, to_read, to_write, c, rank_num, rank, to_unmount;
    MPI_File fh;
    MPI_Status status;
    char *read_buf;
    char *write_buf;

    /* initialize int vars to zero */
    pat, to_read, to_write, c, rank_num, rank, to_unmount = 0;

    /* initialize double read and write vars to zero */
    write_time = 0;
    read_time  = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((c = getopt(argc, argv, opts)) != -1) {

        switch (c)  {
        case 'b':
            /* size of block */
            blk_sz = atol(optarg); break;
        case 'w':
            /* run a write test (set to 1) */
            to_write = atoi(optarg); break;
        case 'r':
            /* run a read test (set to 1) */
            to_read = atoi(optarg); break;
        case 's':
            /* number of blocks each process writes */
            seg_num = atol(optarg); break;
        case 't':
            /* size of each write */
            tran_sz = atol(optarg); break;
        case 'f':
            strcpy(fname, optarg); break;
        case 'p':
            /* 0: N-1 segment/strided, 1: N-N */
            pat = atoi(optarg); break;
        case 'u':
            /* 0: not unmount after finish 1: unmount */
            to_unmount = atoi(optarg); break;
        }
    }

    int mnt_success = unifycr_mount("/tmp", rank, rank_num, 0);

    if (mnt_success != 0 && rank == 0) {
        printf("unifycr_mount call failed\n");
        exit(EIO);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    read_buf = malloc(blk_sz * seg_num);
    write_buf = malloc(tran_sz);
    memset(write_buf, 0, tran_sz);

    if (pat == 1)
        sprintf(tmpfname, "%s%d", fname, rank);
    else
        sprintf(tmpfname, "%s", fname);

    MPI_File_open(MPI_COMM_WORLD, tmpfname, MPI_MODE_CREATE|MPI_MODE_RDWR,
                  MPI_INFO_NULL, &fh);

    if (to_write) {
        printf("calling write test..\n");
        gettimeofday(&write_start, NULL);
        write_test(&fh, &status, write_buf, &blk_sz,
                   &seg_num, &tran_sz, &rank_num, &rank, &pat);
        gettimeofday(&write_end, NULL);
        get_bw(&rank, &rank_num, &blk_sz, &seg_num, write_start, write_end,
               write_time);
    }

    MPI_File_close(&fh);

    if (to_read) {
        printf("calling read test..\n");
        gettimeofday(&read_start, NULL);
        read_test(&fh, &status, read_buf, &blk_sz,
                   &seg_num, &tran_sz, &rank_num, &rank, &pat);
        gettimeofday(&read_end, NULL);
        get_bw(&rank, &rank_num, &blk_sz, &seg_num, read_start, read_end,
               read_time);
    }

    MPI_File_close(&fh);

    MPI_Barrier(MPI_COMM_WORLD);

    if (to_unmount)
        if (rank == 0)
            unifycr_unmount();

    free(write_buf);
    free(read_buf);

    MPI_Finalize();
}
