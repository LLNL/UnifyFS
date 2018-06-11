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
#include <mpi.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <aio.h>
#include <strings.h>
#include <errno.h>
#include <unifycr.h>

#define GEN_STR_LEN 1024

struct timeval read_start, read_end;
double read_time = 0;

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
	int pat, c, rank_num, rank, fd, to_unmount = 0;

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
			   pat = atoi(optarg); break; /* 0: N-1, 1: N-N */
			case 'u':
				to_unmount = atoi(optarg); break;
		  }
	}

	num_reqs = blk_sz*seg_num/tran_sz;
	char *read_buf = malloc(blk_sz * seg_num); /*read buffer*/
    struct aiocb *aiocb_list = (struct aiocb *)malloc(num_reqs\
        * sizeof(struct aiocb));
    struct aiocb **cb_list = (struct aiocb **)malloc (num_reqs * \
      sizeof (struct aiocb *)); /*list of read requests in lio_listio*/

    int mnt_success = unifycr_mount("/tmp", rank, rank_num, 1);

    if (mnt_success != 0 && rank == 0) {
        printf("unifycr mount call failed\n");
        exit(EIO);
    }

	if (pat == 1) {
		sprintf(tmpfname, "%s%d", fname, rank);
	}	else {
		sprintf(tmpfname, "%s", fname);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	fd = open(tmpfname, O_RDONLY, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		printf("open file failure\n");
		fflush(stdout);
		return -1;
	}


	gettimeofday(&read_start, NULL);

	long i, j, offset, rc, cursor = 0;
	for (i = 0; i < seg_num; i++) {
		for (j = 0; j < blk_sz/tran_sz; j++) {
			if (pat == 0)
				offset = i * rank_num * blk_sz +\
					rank * blk_sz + j * tran_sz;
			else if (pat == 1)
				offset = i * blk_sz + j * tran_sz;
			cursor += tran_sz;
			lseek(fd, offset, SEEK_SET);
			rc = read(fd, read_buf + cursor, tran_sz);
			if (rc < 0) {
				printf("read failure\n");
				fflush(stdout);
				return -1;
			}
		}
	}
	gettimeofday(&read_end, NULL);
	read_time = (read_end.tv_sec - read_start.tv_sec)*1000000 \
		+ read_end.tv_usec - read_start.tv_usec;
	read_time = read_time/1000000;

	close(fd);
	MPI_Barrier(MPI_COMM_WORLD);


	if (to_unmount) {
		if (rank == 0)
			unifycr_unmount();
	}

	free(read_buf);

	double read_bw = (double)blk_sz\
			* seg_num / 1048576 / read_time;
	double agg_read_bw;

	double max_read_time, min_read_bw;
	MPI_Reduce(&read_bw, &agg_read_bw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&read_time, &max_read_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);


	min_read_bw=(double)blk_sz*seg_num*rank_num/1048576/max_read_time;
	if (rank == 0) {
			printf("Aggregate Read BW is %lfMB/s,\
			  Min Read BW is %lf\n", \
					agg_read_bw,  min_read_bw);
			fflush(stdout);
	}

	MPI_Finalize();

}
