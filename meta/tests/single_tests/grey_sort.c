#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "mpi.h"
#include "mdhim.h"
#include "db_options.h"

#define KEYS 100000
#define KEY_SIZE 100
#define NUM_KEYS 10000000
int main(int argc, char **argv) {
	int ret;
	int provided;
	int i, j, fd;
	struct mdhim_t *md;
	void **keys;
	int key_lens[KEYS];
	char **values;
	int value_lens[KEYS];
	struct mdhim_brm_t *brm, *brmp;
	struct mdhim_bgetrm_t *bgrm;
	struct timeval start_tv, end_tv;
	char filename[255];
        mdhim_options_t *mdhim_opts;
	struct mdhim_stat *stat, *tmp;

	//Initialize MPI with multiple thread support
	ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (ret != MPI_SUCCESS) {
		printf("Error initializing MPI with threads\n");
		exit(1);
	}

	//Quit if MPI didn't initialize with multiple threads
	if (provided != MPI_THREAD_MULTIPLE) {
                printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
                exit(1);
        }       

        mdhim_opts = mdhim_options_init();
        mdhim_options_set_db_path(mdhim_opts, "./");
        mdhim_options_set_db_name(mdhim_opts, "mdhimTstDB");
        mdhim_options_set_db_type(mdhim_opts, LEVELDB);
        mdhim_options_set_key_type(mdhim_opts, MDHIM_BYTE_KEY); 
        
	//Initialize MDHIM
	md = mdhimInit(MPI_COMM_WORLD, mdhim_opts);
	sprintf(filename, "%s%d", "/scratch/hng/input/input", md->mdhim_rank);
	if ((fd = open(filename, O_RDONLY)) < 0) {
		printf("Error opening input file");
		exit(1);
	}
	if (!md) {
		printf("Error initializing MDHIM\n");
		MPI_Abort(MPI_COMM_WORLD, ret);
		exit(1);
	}	
	
	//Start the timing
	gettimeofday(&start_tv, NULL);

	//Read in the keys from the files and insert them
	for (j = 0; j < NUM_KEYS; j+=KEYS) {
		//Populate the keys and values to insert
		keys = malloc(sizeof(void *) * KEYS);
		values = malloc(sizeof(char *) * KEYS);
		for (i = 0; i < KEYS; i++) {
			keys[i] = malloc(KEY_SIZE);
			ret = read(fd, keys[i], KEY_SIZE);
			if (ret != KEY_SIZE) {
				printf("Error reading in key\n");
			}

			key_lens[i] = KEY_SIZE;
			values[i] = malloc(1);
			*values[i] = 'a';
			value_lens[i] = 1;
		}

		//Insert the keys into MDHIM
		brm = mdhimBPut(md, (void **) keys, key_lens, 
				(void **) values, value_lens, KEYS);
		brmp = brm;
		if (!brm || brm->error) {
		  printf("Rank - %d: Error inserting keys/values into MDHIM\n", md->mdhim_rank);
		} 
		while (brmp) {
			if (brmp->error < 0) {
				printf("Rank: %d - Error inserting key/values info MDHIM\n", md->mdhim_rank);
			}
	
			brmp = brmp->next;
			//Free the message
			mdhim_full_release_msg(brm);
			brm = brmp;
		}

		for (i = 0; i < KEYS; i++) {
			free(keys[i]);
			
		}
		free(keys);
		for (i = 0; i < KEYS; i++) {
			free(values[i]);
			
		}
		free(values);
	}

	close(fd);
	ret = mdhimStatFlush(md);
	if ((ret = im_range_server(md)) != 1) {
		goto done;
	}

	//Iterate through my range server stat hash entries to retrieve all the slices
	HASH_ITER(hh, md->mdhim_rs->mdhim_store->mdhim_store_stats, stat, tmp) {	
		if (!stat) {
			continue;
		}
		
		bgrm = mdhimBGetOp(md, stat->min, KEY_SIZE, 
				   KEYS, MDHIM_GET_NEXT);
		//Check if there is an error
		if (!bgrm || bgrm->error) {
			printf("Rank: %d - Error retrieving values", md->mdhim_rank);
			goto done;
		}
		
		sprintf(filename, "%s%d", "/scratch/hng/output/output_slice_", stat->key);

		if ((fd = open(filename, O_WRONLY | O_CREAT)) < 0) {
			printf("Error opening output file");
			exit(1);
		}
		
		for (i = 0; i < bgrm->num_records && !bgrm->error; i++) {	
			ret = write(fd, bgrm->keys[i], KEY_SIZE);			
			if (ret != KEY_SIZE) {
				printf("Error writing key\n");
			}
		}

		close(fd);
		//Free the message received
		mdhim_full_release_msg(bgrm);
	}	

	gettimeofday(&end_tv, NULL);

done:
	MPI_Barrier(MPI_COMM_WORLD);
	//Quit MDHIM
	ret = mdhimClose(md);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	printf("Took: %u seconds to sort and output %u keys/values\n", 
	       (uint32_t) (end_tv.tv_sec - start_tv.tv_sec), NUM_KEYS);
	return 0;
}
