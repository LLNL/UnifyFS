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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <mpi.h>
#include <math.h>
#include <sys/time.h>
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

#define GEN_STR_LEN 1024

struct timeval readstart, readend;
double readtime = 0;

struct timeval writestart, writeend;
double writetime = 0;

struct timeval metastart, metaend;
double metatime = 0;

struct timeval fsync_data_start, fsync_data_end;

struct timeval fsync_metadata_start, fsync_metadata_end;

double interval = 0;

typedef struct {
  int fid;
  long offset;
  long length;
  char *buf;

}read_req_t;

int main(int argc, char *argv[]) {

	static const char * opts = "h:v:d:f:p:n:";

	int mode, direction, ranknum, rank, dir;
	long h_grid_points, v_grid_points;
	char fname[GEN_STR_LEN] = {0};
	int sz_per_elem;
	int r_ranks, c_ranks;
	int read;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &ranknum);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int c;
	while((c = getopt(argc, argv, opts)) != -1){

		switch (c)  {
			case 'h': /*number of elements along horizontal direction*/
			   h_grid_points = atol(optarg); break;
			case 'v': /*number of elements along vertical direction*/
			   v_grid_points = atol(optarg); break;
			case 'n': /*number of ranks along the x dimension*/
				r_ranks = atoi(optarg); break;
			case 'd': /*0: read 1: write*/
			   direction = atoi(optarg); break;
			case 'f':
			   strcpy(fname, optarg); break;
			case 'p': /*size of each elements*/
				sz_per_elem = atoi(optarg); break;
		  }
		}

		long nr_r_tiles, nr_c_tiles; //# of tiles per dimension
		int dim = 2;

		nr_r_tiles = r_ranks; /*number of tiles on each row, each rank has one tile*/
		nr_c_tiles = ranknum / r_ranks; /*number of tiles on each column, each rank has one tile*/

		long nr_tiles_per_proc = 1;

		long i,j;
		long x_low, y_low;
		long x_high, y_high;
		long x_size, y_size;
		long elems_per_tile;

		/*The following determines the coordinate*/
		x_low = rank % nr_r_tiles;
		y_low = rank / nr_r_tiles;

		x_size = h_grid_points / r_ranks;
		y_size = v_grid_points / (ranknum / r_ranks);
		x_low = x_low * x_size;
		y_low = y_low * y_size;
		x_high = x_low + x_size - 1;
		y_high = y_low + y_size - 1;

		long num_reqs = y_size; /*number of I/O requests*/
		read_req_t *r_w_reqs = (read_req_t *)malloc(num_reqs\
				* sizeof(read_req_t));

		/*initialize the I/O requests*/
		long cursor = 0, tot_sz;
		for (i = y_low; i <= y_high; i++) {
			r_w_reqs[cursor].offset =\
					(x_low + h_grid_points * i) * sz_per_elem;
			r_w_reqs[cursor].length = x_size * sz_per_elem;
			cursor++;
		}
		tot_sz = v_grid_points * h_grid_points * sz_per_elem;

		char *buf = malloc (x_size * sz_per_elem);
		memset(buf, 0, x_size * sz_per_elem);

		MPI_Barrier(MPI_COMM_WORLD);
		unifycr_mount("/tmp", rank, ranknum, 0);
		MPI_Barrier(MPI_COMM_WORLD);

		int fd;
		fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			printf("rank:%d, open failure\n", rank);
			fflush(stdout);
		}
		gettimeofday(&writestart, NULL);
		long rc;
		for (i = 0; i < num_reqs; i++) {
			rc = pwrite(fd, buf, r_w_reqs[i].length, r_w_reqs[i].offset);
			if (rc < 0) {
				printf("rank:%d, write failure\n", rank);
				fflush(stdout);
			}

		}
		gettimeofday(&metastart, NULL);
		fsync(fd);
		gettimeofday(&metaend, NULL);
		metatime += 1000000*(metaend.tv_sec-metastart.tv_sec)\
				+metaend.tv_usec-metastart.tv_usec;
		metatime /= 1000000;
		gettimeofday(&writeend, NULL);
		writetime +=1000000 * (writeend.tv_sec - writestart.tv_sec)\
				+ writeend.tv_usec - writestart.tv_usec;
		writetime = writetime / 1000000;
		if (direction == 1) {
			if (rank == 0)
				unifycr_unmount();
			close(fd);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		double wr_bw = (double)tot_sz/ranknum/1048576/writetime;

		double aggrdbw;
		double aggwrbw;

		double max_wr_time;
		MPI_Reduce(&wr_bw, &aggwrbw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
		MPI_Reduce(&writetime, &max_wr_time, 1, MPI_DOUBLE,\
		 MPI_MAX, 0, MPI_COMM_WORLD);

		double min_wr_bw;
		min_wr_bw=(double)tot_sz/1048576/max_wr_time;

		if (direction == 1) {
			if (rank == 0) {
				printf("Aggregated Write BW is %lf, Min Write BW is %lf\n", \
					 aggwrbw, min_wr_bw);
				fflush(stdout);

			}
		}

		char  *read_buf;
		if (direction == 0) {
			 struct aiocb *aiocb_list = (struct aiocb *)malloc(num_reqs\
		        * sizeof(struct aiocb));
		     struct aiocb **cb_list = (struct aiocb **)malloc (num_reqs * \
		    		 sizeof (struct aiocb *));

			read_buf = (char *)malloc(tot_sz/ranknum);
			memset(read_buf, 0, tot_sz/ranknum);

			gettimeofday(&readstart, NULL);
			cursor = 0;
			for (i = 0; i < num_reqs; i++) {
				aiocb_list[cursor].aio_fildes = fd;
				aiocb_list[cursor].aio_buf = read_buf + \
						i * x_size * sz_per_elem;
				aiocb_list[cursor].aio_nbytes =\
						r_w_reqs[cursor].length;
				aiocb_list[cursor].aio_offset = r_w_reqs[cursor].offset;
				aiocb_list[cursor].aio_lio_opcode = LIO_READ;
				cb_list[cursor] = &aiocb_list[cursor];
				cursor++;
			}

		    int ret = lio_listio(LIO_WAIT, cb_list, num_reqs, NULL);
			gettimeofday(&readend, NULL);
			readtime = (readend.tv_sec - readstart.tv_sec)*1000000 \
				+ readend.tv_usec - readstart.tv_usec;
			readtime = readtime/1000000;

			MPI_Barrier(MPI_COMM_WORLD);
			close(fd);
			if (rank == 0)
				unifycr_unmount();

			double rd_bw = (double)tot_sz/ranknum/1048576/readtime;
			double max_rd_time;
			MPI_Reduce(&rd_bw, &aggrdbw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
			MPI_Reduce(&readtime, &max_rd_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

			double min_rd_bw;
			min_rd_bw=(double)tot_sz/1048576/max_rd_time;
			if (rank == 0) {
					printf("Aggregated Read BW is %lfMB/s,\
					 Aggregated Write BW is %lf, Min Read BW is %lf,\
						 Min Write BW is %lf\n", \
							aggrdbw, aggwrbw, min_rd_bw, min_wr_bw);
					fflush(stdout);
			}

			free(read_buf);
			free(aiocb_list);
			free(cb_list);

		}
		free(r_w_reqs);
		free(buf);
		MPI_Finalize();
}
