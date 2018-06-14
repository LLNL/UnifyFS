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
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <aio.h>
#include <strings.h>


#define GEN_STR_LEN 1024
#define SZ_PER_ELEM 40 /*size of each data point in BTIO*/

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

	static const char * opts = "g:d:f:";

	int mode, direction, ranknum, rank, dir;
	long nr_grid_points;
	char fname[GEN_STR_LEN] = {0};

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &ranknum);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int c;
	while((c = getopt(argc, argv, opts)) != -1){

		switch (c)  {
			case 'g': /*number of elements in each dimension. Adjust this value to
			 	 	    generate various problem sizes*/
			   nr_grid_points = atol(optarg); break;
			case 'd': /*0: write then read 1: only write*/
			   direction = atoi(optarg); break;
			case 'f':
			   strcpy(fname, optarg); break;
		  }
	}

	long **cell_coord, **cell_size, **cell_low, **cell_high;
	long nr_tiles; //# of tiles per dimension

	int dim = 3;
	nr_tiles = sqrt(ranknum); /*for Block-Triangle (BT) partitioning in BTIO, the number of
	 	 	 	 	 	 	    tiles per dimension is the square root of
	 	 	 	 	 	 	    rank count*/

	long nr_tiles_per_proc = nr_tiles; /* the number of cubic cells per proc is nr_tiles*/

	cell_coord = (long **)malloc (nr_tiles_per_proc * sizeof(long *));
	cell_size = (long **)malloc (nr_tiles_per_proc * sizeof(long *));
	cell_low = (long **)malloc(nr_tiles_per_proc * sizeof(long *));
	cell_high = (long **)malloc(nr_tiles_per_proc * sizeof(long *));


	/*The following lines generate the coordinates for each cell in
	 * BT partitioning*/
	long i,j, elem_per_tile;
	for (i = 0; i < nr_tiles_per_proc; i++) {
		cell_coord[i] = (long *)malloc(dim * sizeof(long));
		cell_size[i] = (long *)malloc(dim * sizeof(long));
		cell_low[i] = (long *)malloc(dim * sizeof(long));
		cell_high[i] = (long *)malloc(dim * sizeof(long));
	}

	cell_coord[0][0] = rank%nr_tiles;
	cell_coord[0][1] = rank/nr_tiles;
	cell_coord[0][2] = 0;

	for (c = 1; c < nr_tiles; c++) {
		cell_coord[c][0] = (cell_coord[c-1][0]+1)%nr_tiles;
		cell_coord[c][1] = (cell_coord[c-1][1]-1+nr_tiles)%nr_tiles;
		cell_coord[c][2] = c;
	}

	for (dir = 0; dir < dim; dir++) {
		for (c = 0; c < nr_tiles; c++) {
			cell_coord[c][dir] = cell_coord[c][dir] + 1;
		}
	}

	long elems_per_tile;
	for (dir = 0; dir < dim; dir++) {
		  elems_per_tile = nr_grid_points/nr_tiles;
		  for (c = 0; c < nr_tiles_per_proc; c++ ) {
			cell_size[c][dir] = elems_per_tile;
		    cell_low[c][dir] = \
			  (cell_coord[c][dir] - 1) * elems_per_tile;
		    cell_high[c][dir] =\
		    		cell_low[c][dir] + elems_per_tile - 1;

		 }
	}

	/*calculate the number of I/O requests of writing these
	 * 3D cells*/
	long num_reqs = elems_per_tile\
			* elems_per_tile * nr_tiles_per_proc;

	read_req_t *r_w_reqs = (read_req_t *)malloc(num_reqs\
	 * sizeof(read_req_t));

	/*initialize these I/O requests */
	long cursor = 0, tot_sz;

	for (c = 0; c < nr_tiles_per_proc; c++) {
		for (i = cell_low[c][2]; i <= cell_high[c][2]; i++) {
			for (j = cell_low[c][1]; j <= cell_high[c][1]; j++) {
				r_w_reqs[cursor].offset = (cell_low[c][0] + j * nr_grid_points + \
					i * nr_grid_points * nr_grid_points)*SZ_PER_ELEM;
				r_w_reqs[cursor].length = elems_per_tile * SZ_PER_ELEM;
				cursor++;
			}
		}
	}
	tot_sz = nr_grid_points *\
			nr_grid_points * nr_grid_points * SZ_PER_ELEM;

	char *buf = malloc (elems_per_tile * SZ_PER_ELEM);
	memset(buf, 0, elems_per_tile * SZ_PER_ELEM);

	MPI_Barrier(MPI_COMM_WORLD);
	unifycr_mount("/tmp", rank, ranknum, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	int fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		printf("rank:%d, open failure.\n", rank);
		fflush(stdout);
	}

	gettimeofday(&writestart, NULL);
	long rc;
	for (i = 0; i < num_reqs; i++) {
	/*	if (rank == rank) {
			printf("rank:%d, writing length:%ld, offset:%ld\n", rank, \
					r_w_reqs[i].length, r_w_reqs[i].offset);
			fflush(stdout);
		}*/
		rc = pwrite(fd, buf, r_w_reqs[i].length, r_w_reqs[i].offset);
		if (rc < 0) {
			printf("rank:%d, write error\n", rank);
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
	writetime+=1000000*(writeend.tv_sec-writestart.tv_sec)\
			+writeend.tv_usec-writestart.tv_usec;
	writetime = writetime/1000000;

	MPI_Barrier(MPI_COMM_WORLD);

	double wr_bw = (double)tot_sz/ranknum/1048576/writetime;
	double aggwrbw;
	double max_wr_time;
	MPI_Reduce(&wr_bw, &aggwrbw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&writetime, &max_wr_time, 1, MPI_DOUBLE,\
	 MPI_MAX, 0, MPI_COMM_WORLD);

	double min_wr_bw;
	min_wr_bw=(double)tot_sz /1048576 / max_wr_time;
	if (direction != 0) {/*only write without reading*/
		close(fd);
		if (rank == 0) {
			unifycr_unmount();
			printf("Aggregated Write BW is %lf, Min Write BW is %lf\n", \
				 aggwrbw, min_wr_bw);
			fflush(stdout);
		}
	}

	if (direction == 0) {
		 struct aiocb *aiocb_list = (struct aiocb *)malloc(num_reqs\
	        * sizeof(struct aiocb));
	     struct aiocb **cb_list = (struct aiocb **)malloc (num_reqs * \
	    		 sizeof (struct aiocb *));

		char  *read_buf = (char *)malloc(tot_sz/ranknum);
		memset(read_buf, 0, tot_sz/ranknum);
		gettimeofday(&readstart, NULL);
		cursor = 0;
		for (i = 0; i < num_reqs; i++) {
				aiocb_list[cursor].aio_fildes = fd;
				aiocb_list[cursor].aio_buf = read_buf + \
						i * elems_per_tile * SZ_PER_ELEM;
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
		close(fd);
		MPI_Barrier(MPI_COMM_WORLD);
		if (rank == 0)
			unifycr_unmount();
		free(aiocb_list);
		free(cb_list);


		double aggrdbw;
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
	}
	free(r_w_reqs);
	free(buf);
	MPI_Finalize();
}
