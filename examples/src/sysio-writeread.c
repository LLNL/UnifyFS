/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <mpi.h>
#include <sys/time.h>
#include <aio.h>

#ifndef DISABLE_UNIFYFS
# include <unifyfs.h>
#endif

#define TEST_STR_LEN 1024

struct timeval write_start, write_end;
double write_time;

struct timeval meta_start;
double meta_time;

struct timeval read_start, read_end;
double read_time;

typedef struct {
    int fid;
    long offset;
    long length;
    char* buf;
} read_req_t;

int main(int argc, char* argv[])
{
    static const char* opts = "b:f:m:n:p:t:u:";
    char tmpfname[TEST_STR_LEN], fname[TEST_STR_LEN], mntpt[TEST_STR_LEN];
    size_t blk_sz = 0, num_blk = 0, tran_sz = 0, num_reqs = 0;
    size_t index, i, j, offset = 0;
    ssize_t rc;
    int ret;
    int pat = 0, c, num_rank, rank, fd, use_unifyfs = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &num_rank);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((c = getopt(argc, argv, opts)) != -1) {
        switch (c) {
        case 'b': /*size of block*/
            blk_sz = atol(optarg);
            break;
        case 'f':
            strcpy(fname, optarg);
            break;
        case 'm':
            strcpy(mntpt, optarg);
            break;
        case 'n': /*number of blocks each process writes*/
            num_blk = atol(optarg);
            break;
        case 'p':
            pat = atoi(optarg);
            break; /* 0: N-1 segment/strided, 1: N-N*/
        case 't': /*size of each write */
            tran_sz = atol(optarg);
            break;
        case 'u': /* use unifyfs */
            use_unifyfs = atoi(optarg);
            break;
        }
    }

    if (use_unifyfs) {
        strcpy(mntpt, "/unifyfs");
    } else {
        strcpy(mntpt, "/tmp");
    }

    if ((pat < 0) || (pat > 1)) {
        printf("unsupported I/O pattern");
        fflush(stdout);
        return -1;
    }

    if (blk_sz == 0) {
        blk_sz = 1048576;    /* 1 MiB block size */
    }

    if (num_blk == 0) {
        num_blk = 64;    /* 64 blocks per process */
    }

    if (tran_sz == 0) {
        tran_sz = 32768;    /* 32 KiB IO operation size */
    }

    size_t n_tran_per_blk = blk_sz / tran_sz;
    double rank_mib = (double)(blk_sz * num_blk) / 1048576;
    double total_mib = rank_mib * num_rank;

    char* buf = malloc(tran_sz);
    if (buf == NULL) {
        return -1;
    }

    int byte = (int)'0' + rank;
    memset(buf, byte, tran_sz);

#ifndef DISABLE_UNIFYFS
    if (use_unifyfs) {
        ret = unifyfs_mount(mntpt, rank, num_rank, 0);
        if (UNIFYFS_SUCCESS != ret) {
            MPI_Abort(MPI_COMM_WORLD, ret);
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
#endif

    if (pat == 0) { // N-1
        sprintf(tmpfname, "%s/%s", mntpt, fname);
    } else { // N-N
        sprintf(tmpfname, "%s/%s%d", mntpt, fname, rank);
    }

    int open_flags = O_CREAT | O_RDWR;
    fd = open(tmpfname, open_flags, 0644);
    if (fd < 0) {
        printf("open file failure\n");
        fflush(stdout);
        return -1;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&write_start, NULL);

    for (i = 0; i < num_blk; i++) {
        for (j = 0; j < n_tran_per_blk; j++) {
            if (pat == 0)
                offset = (i * blk_sz * num_rank)
                         + (rank * blk_sz) + (j * tran_sz);
            else {
                offset = (i * blk_sz) + (j * tran_sz);
            }
            rc = pwrite(fd, buf, tran_sz, offset);

            if (rc < 0) {
                perror("pwrite failed");
            }
        }
    }

    gettimeofday(&meta_start, NULL);
    fsync(fd);

    gettimeofday(&write_end, NULL);
    free(buf);

    meta_time += 1000000 * (write_end.tv_sec - meta_start.tv_sec)
                 + write_end.tv_usec - meta_start.tv_usec;
    meta_time /= 1000000;

    write_time += 1000000 * (write_end.tv_sec - write_start.tv_sec)
                  + write_end.tv_usec - write_start.tv_usec;
    write_time = write_time / 1000000;

    double agg_write_bw, max_write_time;
    double write_bw = rank_mib / write_time;
    MPI_Reduce(&write_bw, &agg_write_bw, 1, MPI_DOUBLE, MPI_SUM,
               0, MPI_COMM_WORLD);
    MPI_Reduce(&write_time, &max_write_time, 1, MPI_DOUBLE, MPI_MAX,
               0, MPI_COMM_WORLD);
    double min_write_bw = total_mib / max_write_time;

    if (rank == 0) {
        printf("Aggregate Write BW is %.3lf MiB/s\n"
               "  Minimum Write BW is %.3lf MiB/s\n\n",
               agg_write_bw, min_write_bw);
        fflush(stdout);
    }

    /* read buffer */
    char* read_buf = calloc(num_blk, blk_sz);

    /* list of read requests for lio_listio */
    num_reqs = num_blk * n_tran_per_blk;

    struct aiocb* aiocb_list = (struct aiocb*) calloc(num_reqs,
                               sizeof(struct aiocb));

    struct aiocb** cb_list = (struct aiocb**) calloc(num_reqs,
                             sizeof(struct aiocb*));

    if ((read_buf == NULL) || (aiocb_list == NULL) || (cb_list == NULL)) {
        return -1;
    }

    index = 0;

    if (pat == 0) { // N-1
        for (i = 0; i < num_blk; i++) {
            for (j = 0; j < n_tran_per_blk; j++) {
                aiocb_list[index].aio_fildes = fd;
                aiocb_list[index].aio_buf = read_buf + (index * tran_sz);
                aiocb_list[index].aio_nbytes = tran_sz;
                aiocb_list[index].aio_offset = (i * blk_sz * num_rank)
                                               + (rank * blk_sz)
                                               + (j * tran_sz);
                aiocb_list[index].aio_lio_opcode = LIO_READ;
                cb_list[index] = &aiocb_list[index];
                index++;
            }
        }
    } else { // N-N
        for (i = 0; i < num_blk; i++) {
            for (j = 0; j < n_tran_per_blk; j++) {
                aiocb_list[index].aio_fildes = fd;
                aiocb_list[index].aio_buf = read_buf + (index * tran_sz);
                aiocb_list[index].aio_nbytes = tran_sz;
                aiocb_list[index].aio_offset = (i * blk_sz) + (j * tran_sz);
                aiocb_list[index].aio_lio_opcode = LIO_READ;
                cb_list[index] = &aiocb_list[index];
                index++;
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    gettimeofday(&read_start, NULL);

    ret = lio_listio(LIO_WAIT, cb_list, num_reqs, NULL);
    if (ret < 0) {
        perror("lio_listio failed");
    }

    gettimeofday(&read_end, NULL);

    close(fd);
    free(read_buf);

    read_time = (read_end.tv_sec - read_start.tv_sec) * 1000000
                + read_end.tv_usec - read_start.tv_usec;
    read_time = read_time / 1000000;

    double agg_read_bw, max_read_time;
    double read_bw = rank_mib / read_time;
    MPI_Reduce(&read_bw, &agg_read_bw, 1, MPI_DOUBLE, MPI_SUM,
               0, MPI_COMM_WORLD);
    MPI_Reduce(&read_time, &max_read_time, 1, MPI_DOUBLE, MPI_MAX,
               0, MPI_COMM_WORLD);
    double min_read_bw = total_mib / max_read_time;

    if (rank == 0) {
        printf("Aggregate Read BW is %.3lf MiB/s\n"
               "  Minimum Read BW is %.3lf MiB/s\n\n",
               agg_read_bw, min_read_bw);
        fflush(stdout);
    }

#ifndef DISABLE_UNIFYFS
    if (use_unifyfs) {
        unifyfs_unmount();
    }
#endif

    MPI_Finalize();

    return 0;
}
