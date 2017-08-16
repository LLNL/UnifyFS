/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 * Written by
 * 	Teng Wang tw15g@my.fsu.edu
 * 	Adam Moody moody20@llnl.gov
 * 	Weikuan Yu wyu3@fsu.edu
 * 	Kento Sato kento@llnl.gov
 * 	Kathryn Mohror. kathryn@llnl.gov
 * 	LLNL-CODE-728877.
 * All rights reserved.
 *
 * This file is part of BurstFS For details, see https://github.com/llnl/burstfs.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file COPYRIGHT
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <mpi.h>
#include <sys/time.h>

#define GEN_STR_LEN 1024

struct timeval read_start, read_end;
double readtime = 0;

struct timeval write_start, write_end;
double write_time = 0;

struct timeval meta_start, meta_end;
double meta_time = 0;

typedef struct {
  int fid;
  long offset;
  long length;
  char *buf;

}read_req_t;

int main(int argc, char *argv[]) {

	static const char * opts = "b:s:t:f:p:u:";
	char tmpfname[GEN_STR_LEN], fname[GEN_STR_LEN];
	long blk_sz, seg_num, tran_sz, num_reqs;
	int pat, c, rank_num, rank, direction, fd, \
		to_unmount = 0;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	while((c = getopt(argc, argv, opts)) != -1){

		switch (c)  {
			case 'b': /*size of block*/
			   blk_sz = atol(optarg); break;
			case 's': /*number of blocks each process writes*/
			   seg_num = atol(optarg); break;
			case 't': /*size of each write*/
			   tran_sz = atol(optarg); break;
			case 'f':
			   strcpy(fname, optarg); break;
			case 'p':
			   pat = atoi(optarg); break; /* 0: N-1 segment/strided, 1: N-N*/
			case 'u':
			   to_unmount = atoi(optarg); break; /*0: not unmount after finish 1: unmount*/
		  }
		}

	burstfs_mount("/tmp", rank, rank_num, 0, 1);

	char *buf = malloc (tran_sz);
	if (buf == NULL)
		return -1;
	memset(buf, 0, tran_sz);

	MPI_Barrier(MPI_COMM_WORLD);

	if (pat == 1) {
		sprintf(tmpfname, "%s%d", fname, rank);
	}	else {
		sprintf(tmpfname, "%s", fname);
	}

	fd = open(tmpfname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		printf("open file failure\n");
		fflush(stdout);
		return -1;
	}

	gettimeofday(&write_start, NULL);
	long i, j, offset, rc;
	for (i = 0; i < seg_num; i++) {
		for (j = 0; j<blk_sz/tran_sz; j++) {
			if (pat == 0)
				offset = i * rank_num * blk_sz +\
					rank * blk_sz + j * tran_sz;
			else if (pat == 1)
				offset = i * blk_sz + j * tran_sz;
			lseek(fd, offset, SEEK_SET);
                        printf("right before write call\n"); 
			rc = write(fd, buf, tran_sz);
                        printf("right after write call\n");
			if (rc < 0) {
				printf("write failure\n");
				fflush(stdout);
				return -1;
			}
		}
	}

	gettimeofday(&meta_start, NULL);
	fsync(fd);
	gettimeofday(&meta_end, NULL);
	meta_time += 1000000 * (meta_end.tv_sec - meta_start.tv_sec)\
			+ meta_end.tv_usec - meta_start.tv_usec;
	meta_time /= 1000000;

	gettimeofday(&write_end, NULL);
	write_time+=1000000 * (write_end.tv_sec - write_start.tv_sec)\
			+ write_end.tv_usec - write_start.tv_usec;
	write_time = write_time/1000000;
	close(fd);
	MPI_Barrier(MPI_COMM_WORLD);

	if (to_unmount) {
		if (rank == 0) {
			burstfs_unmount();
		}
	}

	double write_bw = (double)blk_sz\
			* seg_num / 1048576 /write_time;
	double agg_write_bw;
	MPI_Reduce(&write_bw, &agg_write_bw, 1, MPI_DOUBLE,\
			MPI_SUM, 0, MPI_COMM_WORLD);

	double max_write_time;
	MPI_Reduce(&write_time, &max_write_time, 1, MPI_DOUBLE,\
			MPI_MAX, 0, MPI_COMM_WORLD);

	double min_write_bw;
	min_write_bw=(double)blk_sz\
			* seg_num * rank_num / 1048576 / max_write_time;

	if (rank == 0) {
			printf("Aggregate Write BW is %lfMB/s, Min Write BW is %lfMB/s\n",\
					agg_write_bw, min_write_bw);
			fflush(stdout);
	}
	free(buf);

	MPI_Finalize();
}
