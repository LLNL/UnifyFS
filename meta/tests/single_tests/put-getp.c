#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "mdhim.h"

int main(int argc, char **argv) {
	int ret;
	int provided = 0;
	struct mdhim_t *md;
	int key;
	int value;
	struct mdhim_brm_t *brm;
	struct mdhim_bgetrm_t *bgrm;
	int i;
	int keys_per_rank = 5;
	char     *db_path = " ";
	char     *db_name = "mdhim";
	int      dbug = MLOG_CRIT;
	mdhim_options_t *db_opts; // Local variable for db create options to be passed
	int db_type = LEVELDB; // (data_store.h) 
	MPI_Comm comm;

	// Create options for DB initialization
	db_opts = mdhim_options_init();
	mdhim_options_set_db_path(db_opts, db_path);
	mdhim_options_set_db_name(db_opts, db_name);
	mdhim_options_set_db_type(db_opts, db_type);
	mdhim_options_set_key_type(db_opts, MDHIM_INT_KEY);
	mdhim_options_set_debug_level(db_opts, dbug);

	ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (ret != MPI_SUCCESS) {
		printf("Error initializing MPI with threads\n");
		exit(1);
	}

	if (provided != MPI_THREAD_MULTIPLE) {
                printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
                exit(1);
        }

	comm = MPI_COMM_WORLD;
	md = mdhimInit(&comm, db_opts);
	if (!md) {
		printf("Error initializing MDHIM\n");
		exit(1);
	}	

	//Put the keys and values
	for (i = 0; i < keys_per_rank; i++) {
		key = keys_per_rank * md->mdhim_rank + i;
		value = md->mdhim_rank + i;
		brm = mdhimPut(md, &key, sizeof(key), 
			       &value, sizeof(value), 
			       NULL, NULL);
		if (!brm || brm->error) {
			printf("Error inserting key/value into MDHIM\n");
		} else {
			printf("Rank: %d put key: %d with value: %d\n", md->mdhim_rank, key, value);
		}
	}

	//Commit the database
	ret = mdhimCommit(md, md->primary_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error committing MDHIM database\n");
	} else {
		printf("Committed MDHIM database\n");
	}

	//Get the stats
	ret = mdhimStatFlush(md, md->primary_index);
	if (ret != MDHIM_SUCCESS) {
		printf("Error getting stats\n");
	} else {
		printf("Got stats\n");
	}

	//Get the values using get_prev
	for (i = keys_per_rank; i > 0; i--) {
		value = 0;
		key = keys_per_rank * md->mdhim_rank + i;
		bgrm = mdhimBGetOp(md, md->primary_index, 
				   &key, sizeof(int), 1, MDHIM_GET_PREV);
		if (!bgrm || bgrm->error) {
			printf("Error getting value for key: %d from MDHIM\n", key);
		} else if (bgrm->keys[0] && bgrm->values[0]) {
			printf("Rank: %d successfully got key: %d with value: %d from MDHIM\n", 
			       md->mdhim_rank,
			       *((int *) bgrm->keys[0]),
			       *((int *) bgrm->values[0]));
		}
	}

	ret = mdhimClose(md);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
