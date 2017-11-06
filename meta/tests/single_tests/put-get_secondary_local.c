#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "mdhim.h"
#include "mdhim_options.h"

#define SLICE_SIZE 100
#define SECONDARY_SLICE_SIZE 5

int main(int argc, char **argv) {
	int ret;
	int provided = 0;
	struct mdhim_t *md;
	uint32_t key, **secondary_keys;
	int value, *secondary_key_lens;
	struct mdhim_brm_t *brm;
	struct mdhim_bgetrm_t *bgrm;
        mdhim_options_t *db_opts;
	struct index_t *secondary_local_index;
	struct secondary_info *secondary_info;
	MPI_Comm comm;

	ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (ret != MPI_SUCCESS) {
		printf("Error initializing MPI with threads\n");
		exit(1);
	}

	if (provided != MPI_THREAD_MULTIPLE) {
                printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
                exit(1);
        }
        
        db_opts = mdhim_options_init();
        mdhim_options_set_db_path(db_opts, "./");
        mdhim_options_set_db_name(db_opts, "mdhimTstDB");
        mdhim_options_set_db_type(db_opts, LEVELDB);
        mdhim_options_set_key_type(db_opts, MDHIM_INT_KEY); //Key_type = 1 (int)
	mdhim_options_set_debug_level(db_opts, MLOG_CRIT);

	comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);
	if (!md) {
		printf("Error initializing MDHIM\n");
		exit(1);
	}	
	
	//Put the primary keys and values
	key = 100 * (md->mdhim_rank + 1);
	value = 500 * (md->mdhim_rank + 1);
	
	//Create a secondary index on only one range server
	secondary_local_index = create_local_index(md, LEVELDB, 
						   MDHIM_INT_KEY, NULL);
	secondary_keys = malloc(sizeof(uint32_t *));	
	secondary_keys[0] = malloc(sizeof(uint32_t));
	*secondary_keys[0] = md->mdhim_rank + 1;
	secondary_keys[1] = malloc(sizeof(uint32_t));
	*secondary_keys[1] = md->mdhim_rank + 2;
	secondary_key_lens = malloc(sizeof(int) * 2);
	secondary_key_lens[0] = sizeof(uint32_t);
	secondary_key_lens[1] = sizeof(uint32_t);
	secondary_info = mdhimCreateSecondaryInfo(secondary_local_index, 
						  (void **) secondary_keys, 
						  secondary_key_lens, 2, 
						  SECONDARY_LOCAL_INFO);
	brm = mdhimPut(md, &key, sizeof(key), 
		       &value, sizeof(value), 
		       NULL, secondary_info);
	if (!brm || brm->error) {
		printf("Error inserting key/value into MDHIM\n");
	} else {
		printf("Successfully inserted key/value into MDHIM\n");
	}

	//Release the received message
	mdhim_full_release_msg(brm);

	//Commit the database
	ret = mdhimCommit(md, md->primary_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error committing MDHIM database\n");
	} else {
		printf("Committed MDHIM database\n");
	}

	//Get the stats for the secondary index so the client figures out who to query
	ret = mdhimStatFlush(md, secondary_local_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error getting stats\n");
	} else {
		printf("Got stats\n");
	}

	//Get the primary key values from the secondary local key
	value = 0;
	bgrm = mdhimGet(md, secondary_local_index, 
			&secondary_keys[0][0], 
			secondary_key_lens[0], 
			MDHIM_GET_PRIMARY_EQ);
	if (!bgrm || bgrm->error) {
		printf("Error getting value for key: %d from MDHIM\n", key);
	} else if (bgrm->value_lens[0]) {
		printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
	}

	mdhim_full_release_msg(bgrm);
	ret = mdhimClose(md);
	mdhim_options_destroy(db_opts);
	mdhimReleaseSecondaryInfo(secondary_info);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}

	free(secondary_keys[0]);
	free(secondary_keys[1]);
	free(secondary_keys);
	free(secondary_key_lens);
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
