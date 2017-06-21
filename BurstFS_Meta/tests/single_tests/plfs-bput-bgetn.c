#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include "mpi.h"
#include "mdhim.h"

#define KEYS 1000000
//#define TOTAL_KEYS 2083334
#define TOTAL_KEYS 1000000
#define SLICE_SIZE 1000000

uint64_t **keys;
int *key_lens;
uint64_t **values;
int *value_lens;

void start_record(struct timeval *start) {
	gettimeofday(start, NULL);
}

void end_record(struct timeval *end) {
	gettimeofday(end, NULL);
}

void add_time(struct timeval *start, struct timeval *end, long double *time) {
  	long double elapsed = (long double) (end->tv_sec - start->tv_sec) + 
		((long double) (end->tv_usec - start->tv_usec)/1000000.0);
	*time += elapsed;
}

void gen_keys_values(int rank, int total_keys) {
	int i = 0;
	for (i = 0; i < KEYS; i++) {
		keys[i] = malloc(sizeof(uint64_t));	
		*keys[i] = i + (uint64_t) ((uint64_t) rank * (uint64_t)TOTAL_KEYS) + total_keys;
		key_lens[i] = sizeof(uint64_t);
		values[i] = malloc(sizeof(int));
		*values[i] = 1;
		value_lens[i] = sizeof(int);
	}
}

void free_key_values() {
	int i;

	for (i = 0; i < KEYS; i++) {
		free(keys[i]);
		free(values[i]);
	}
}

int main(int argc, char **argv) {
	int ret;
	int provided;
	int i;
	struct mdhim_t *md;
	struct mdhim_brm_t *brm, *brmp;
	struct mdhim_bgetrm_t *bgrm;
	struct timeval start_tv, end_tv;
	char     *db_name = "mdhimTstDB-";
	int      dbug = MLOG_CRIT; //MLOG_CRIT=1, MLOG_DBG=2
	mdhim_options_t *db_opts; // Local variable for db create options to be passed
	int db_type = LEVELDB; // (data_store.h) 
	int size;
	long double flush_time = 0;
	long double put_time = 0;
	long double get_time = 0;
	int total_keys = 0;
	int round = 0;
	char *paths[] = {"/tmp/"};
	MPI_Comm comm;

	// Create options for DB initialization
	db_opts = mdhim_options_init();
	mdhim_options_set_db_paths(db_opts, paths, 1);
	mdhim_options_set_db_name(db_opts, db_name);
	mdhim_options_set_db_type(db_opts, db_type);
	mdhim_options_set_key_type(db_opts, MDHIM_LONG_INT_KEY);
	mdhim_options_set_max_recs_per_slice(db_opts, SLICE_SIZE);
        mdhim_options_set_server_factor(db_opts, 1);
	mdhim_options_set_debug_level(db_opts, dbug);

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

	//Initialize MDHIM
	comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);
	if (!md) {
		printf("Error initializing MDHIM\n");
		MPI_Abort(MPI_COMM_WORLD, ret);
		exit(1);
	}
	
	key_lens = malloc(sizeof(uint64_t) * KEYS);
	value_lens = malloc(sizeof(int) * KEYS);
	keys = malloc(sizeof(uint64_t *) * KEYS);
	values = malloc(sizeof(int *) * KEYS);
	MPI_Comm_size(md->mdhim_comm, &size);
	MPI_Barrier(MPI_COMM_WORLD);	
	//record the start time
	start_record(&start_tv);
	while (total_keys != TOTAL_KEYS) {
		//Populate the keys and values to insert
		gen_keys_values(md->mdhim_rank, total_keys);
		//Insert the keys into MDHIM
		brm = mdhimBPut(md, md->primary_index, (void **) keys, key_lens,  
				(void **) values, value_lens, KEYS);
		//		MPI_Barrier(MPI_COMM_WORLD);

		//Iterate through the return messages to see if there is an error and to free it
		brmp = brm;
                if (!brmp || brmp->error) {
                        printf("Rank - %d: Error inserting keys/values into MDHIM\n", md->mdhim_rank);
                } 
		while (brmp) {
			if (brmp->error < 0) {
				printf("Rank: %d - Error inserting key/values info MDHIM\n", md->mdhim_rank);
			}

			brm = brmp;			
			brmp = brmp->next;
			//Free the message
			mdhim_full_release_msg(brm);

		}

		free_key_values();
		total_keys += KEYS;
		round++;
	}
	
	//Record the end time
	end_record(&end_tv);
	//Add the final time
	add_time(&start_tv, &end_tv, &put_time);
	MPI_Barrier(MPI_COMM_WORLD);
	//Get the stats
	start_record(&start_tv);
	ret = mdhimStatFlush(md, md->primary_index);
	//	MPI_Barrier(MPI_COMM_WORLD);
	end_record(&end_tv);
	add_time(&start_tv, &end_tv, &flush_time);
	
	if (ret != MDHIM_SUCCESS) {
		printf("Error getting stats from MDHIM database\n");
	} else {
//			printf("Got stats\n");
	}
	
	total_keys = 0;
	while (total_keys != TOTAL_KEYS) {	
		//Populate the keys and values we expect to retrieve
		gen_keys_values(md->mdhim_rank, total_keys);
		start_record(&start_tv);
		//Get the keys and values back starting from and including key[0]
		bgrm = mdhimBGetOp(md, md->primary_index, keys[0], sizeof(uint64_t), 
				   KEYS, MDHIM_GET_NEXT);
		//	        MPI_Barrier(MPI_COMM_WORLD);
		end_record(&end_tv);
		add_time(&start_tv, &end_tv, &get_time);
		//Check if there is an error
		if (!bgrm || bgrm->error) {
			printf("Rank: %d - Error retrieving values starting at: %llu", 
			       md->mdhim_rank, (long long unsigned int) *keys[0]);
			goto done;
		}
	
		//Validate that the data retrieved is the correct data
		for (i = 0; i < bgrm->num_keys && !bgrm->error; i++) {						
			assert(*(uint64_t *)bgrm->keys[i] == *keys[i]);
			assert(*(int *)bgrm->values[i] == *values[i]);
		}
	
		//Free the message received
		mdhim_full_release_msg(bgrm);
		free_key_values();	       
		total_keys += KEYS;
		round++;
	}

	free(key_lens);
	free(keys);
	free(values);
	free(value_lens);
done:
	MPI_Barrier(MPI_COMM_WORLD);
	//Quit MDHIM
	ret = mdhimClose(md);
	mdhim_options_destroy(db_opts);
	gettimeofday(&end_tv, NULL);

	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	printf("Took: %Lf seconds to put %d keys\n", 
	       put_time, TOTAL_KEYS);
	printf("Took: %Lf seconds to get %d keys/values\n", 
	       get_time, TOTAL_KEYS);
	printf("Took: %Lf seconds to stat flush\n", 
	       flush_time);

	return 0;
}
