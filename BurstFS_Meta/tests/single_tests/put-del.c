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
	char     *db_path = "./";
	char     *db_name = "mdhimTstDB";
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
	key = 20 * (md->mdhim_rank + 1);
	value = 1000 * (md->mdhim_rank + 1);
	brm = mdhimPut(md, &key, sizeof(key), 
		       &value, sizeof(value), 
		       NULL, NULL);
	if (!brm || brm->error) {
		printf("Error inserting key/value into MDHIM\n");
	} else {
		printf("Successfully inserted key/value into MDHIM\n");
	}

	brm = mdhimDelete(md, md->primary_index, &key, sizeof(key));
	if (!brm || brm->error) {
		printf("Error deleting key/value from MDHIM\n");
	} else {
		printf("Successfully deleted key/value into MDHIM\n");
	}

	//Get the values
	value = 0;
	bgrm = mdhimGet(md, md->primary_index, &key, sizeof(key), MDHIM_GET_EQ);
	if (!bgrm || bgrm->error) {
		printf("Error getting value for key: %d from MDHIM\n", key);
	} else if (bgrm->value_lens[0]) {
		printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
	}

	ret = mdhimClose(md);
	if (ret != MDHIM_SUCCESS) {
		printf("Error closing MDHIM\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
