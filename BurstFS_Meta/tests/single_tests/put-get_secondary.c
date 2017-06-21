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
	uint32_t skey;
	int value, *secondary_key_lens;
	struct mdhim_brm_t *brm;
	struct mdhim_bgetrm_t *bgrm;
        mdhim_options_t *db_opts;
	struct index_t *secondary_index;
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

	//Set the primary keys and values
	key = 100 * (md->mdhim_rank + 1);
	value = 500 * (md->mdhim_rank + 1);
	//Set the secondary keys and values
	secondary_keys = malloc(sizeof(uint32_t *));
	
	secondary_keys[0] = malloc(sizeof(uint32_t));
	*secondary_keys[0] = md->mdhim_rank + 1;
	secondary_key_lens = malloc(sizeof(int));
	secondary_key_lens[0] = sizeof(uint32_t);

	//Create the secondary remote index
	secondary_index = create_global_index(md, 2, SECONDARY_SLICE_SIZE, LEVELDB, 
					      MDHIM_INT_KEY, NULL);
	//Create the secondary info struct
	secondary_info = mdhimCreateSecondaryInfo(secondary_index,
						  (void **) secondary_keys, 
						  secondary_key_lens, 
						  1, SECONDARY_GLOBAL_INFO);
	//Put the primary and secondary keys
	brm = mdhimPut(md, 
		       &key, sizeof(key), 
		       &value, sizeof(value), 
		       secondary_info,
		       NULL);
	
	if (!brm || brm->error) {
		printf("Error inserting key/value into MDHIM\n");
	} else {
		printf("Successfully inserted key/value into MDHIM\n");
	}
	mdhim_full_release_msg(brm);
	
	//Put another secondary key
	skey = 2 * (md->mdhim_rank + 1);
	brm = mdhimPutSecondary(md, 
				secondary_index,
				/*Secondary key */
				&skey, sizeof(skey),  
				/* Primary key */
				&key, sizeof(key));
	
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

	//Get the primary key values from the secondary key
	value = 0;
	
	bgrm = mdhimGet(md, secondary_index, 
			secondary_keys[0], secondary_key_lens[0], 
			MDHIM_GET_PRIMARY_EQ);
	if (!bgrm || bgrm->error) {
		printf("Error getting value for key: %d from MDHIM\n", key);
	} else if (bgrm->value_lens[0]) {
		printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
	}

	mdhim_full_release_msg(bgrm);
	ret = mdhimClose(md);
	mdhim_options_destroy(db_opts);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}

	free(secondary_keys[0]);
	free(secondary_keys);
	free(secondary_key_lens);
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
