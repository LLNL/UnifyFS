/*
 * =====================================================================================
 *
 *       Filename:  index_name.c
 *
 *    Description:  Testing Index Naming
 *
 *        Version:  1.0
 *        Created:  11/26/2014 12:35:26 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Aaron Caldwell (), caldwellfans@gmail.com
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "mdhim.h"
#include "mdhim_options.h"

#define SLICE_SIZE 100
#define SECONDAERY_SLICE_SIZE 5

int main(int argc, char **argv) {
    int ret;
    int provided=0;
    struct mdhim_t *md;
    uint32_t key, key2, key3, **secondary_keys, **secondary_keys2, **secondary_keys3;
    int value, *secondary_key_lens, *secondary_key_lens2, *secondary_key_lens3;
    struct mdhim_brm_t *brm;
    struct mdhim_bgetrm_t *bgrm;
    mdhim_options_t *db_opts;
    struct index_t *secondary_local_index, *secondary_local_index2, *secondary_local_index3, *secondary_local_index_by_name;
    struct secondary_info *secondary_info, *secondary_info2, *secondary_info3;
    MPI_Comm comm;

    ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (ret != MPI_SUCCESS) {
        printf("Error initializing MPI with threads\n");
        exit(1);
    }

    if ( provided != MPI_THREAD_MULTIPLE) {
        printf("Not able to enable MPI_THREAD_MULTIPLE mode.\n");
        exit(1);
    }

    db_opts = mdhim_options_init();
    mdhim_options_set_db_path(db_opts, "./");
    mdhim_options_set_db_name(db_opts, "mdhimInxName");
    mdhim_options_set_db_type(db_opts, LEVELDB);
    mdhim_options_set_key_type(db_opts, MDHIM_INT_KEY); //Key_type = 1 (int)
    mdhim_options_set_debug_level(db_opts, MLOG_CRIT);

    comm = MPI_COMM_WORLD;
    md = mdhimInit(&comm, db_opts);
    if(!md) {
        printf("ERROR initializing MDHIM\n");
        exit(1);
    }

    key = 100 * (md->mdhim_rank + 1);
    value = 500 * (md->mdhim_rank + 1);

    secondary_keys = malloc(sizeof(uint32_t *));
    secondary_keys[0] = malloc(sizeof(uint32_t));
    *secondary_keys[0] = md->mdhim_rank + 1;
    secondary_key_lens = malloc(sizeof(int));
    secondary_key_lens[0] = sizeof(uint32_t);

    secondary_keys2 = malloc(sizeof(uint32_t *));
    secondary_keys2[0] = malloc(sizeof(uint32_t));
    *secondary_keys2[0] = md->mdhim_rank + 1;
    secondary_key_lens2 = malloc(sizeof(int));
    secondary_key_lens2[0] = sizeof(uint32_t);

    secondary_keys3 = malloc(sizeof(uint32_t *));
    secondary_keys3[0] = malloc(sizeof(uint32_t));
    *secondary_keys3[0] = md->mdhim_rank + 1;
    secondary_key_lens3 = malloc(sizeof(int));
    secondary_key_lens3[0] = sizeof(uint32_t);

    //Create a secondary index on only one range server
    secondary_local_index = create_local_index(md, LEVELDB, MDHIM_INT_KEY,
                                                "Test1");
    secondary_local_index2 = create_local_index(md, LEVELDB, MDHIM_INT_KEY,
                                                "Test2");
    secondary_local_index3 = create_local_index(md, LEVELDB, MDHIM_INT_KEY,
                                                "Test3");

    secondary_info = mdhimCreateSecondaryInfo(secondary_local_index,
                                              (void **) secondary_keys,
                                              secondary_key_lens, 1,
                                              SECONDARY_LOCAL_INFO);

    secondary_info2 = mdhimCreateSecondaryInfo(secondary_local_index2,
                                              (void **) secondary_keys2,
                                              secondary_key_lens2, 1,
                                              SECONDARY_LOCAL_INFO);

    secondary_info3 = mdhimCreateSecondaryInfo(secondary_local_index3,
                                              (void **) secondary_keys3,
                                              secondary_key_lens3, 1,
                                              SECONDARY_LOCAL_INFO);


    brm = mdhimPut(md, &key, sizeof(key), &value, sizeof(value), NULL, secondary_info);

    if (!brm || brm->error) {
        printf("Error inserting key/value into MDHIM\n");
    } else {
        printf("Successfully inserted key/value into MDHIM\n");
    }

    // Release the received message
    mdhim_full_release_msg(brm);

    // Insert a new key with the second secondary key
    key2 = 200 * (md->mdhim_rank + 1);
    brm = mdhimPut(md, &key2, sizeof(key2), &value, sizeof(value), NULL, secondary_info2);

    if (!brm || brm->error) {
        printf("Secondary Error inserting key/value into MDHIM\n");
    } else {
        printf("Successfully inserted key/value into MDHIM\n");
    }

    // Release the received message
    mdhim_full_release_msg(brm);

    // Insert a new key with the third secondary key
    key3 = 300 * (md->mdhim_rank + 1);
    brm = mdhimPut(md, &key3, sizeof(key3), &value, sizeof(value), NULL, secondary_info3);

    if (!brm || brm->error) {
        printf("Secondary Error inserting key/value into MDHIM\n");
    } else {
        printf("Successfully inserted key/value into MDHIM\n");
    }

    // Release the received message
    mdhim_full_release_msg(brm);

    // Commit the database
    ret = mdhimCommit(md, md->primary_index);
    if(ret != MDHIM_SUCCESS) {
        printf("Error commiting MDHIM database\n");
    } else {
        printf("Committed MDHIM database\n");
    }

    // Get the stats for the secondary index so the client figures out who to query
    ret = mdhimStatFlush(md, secondary_local_index);
    if(ret != MDHIM_SUCCESS) {
        printf("Error getting stats\n");
    } else {
        printf("Got stats\n");
    }

    // Get the stats for the secondary index so the client figures out who to query
    ret = mdhimStatFlush(md, secondary_local_index2);
    if(ret != MDHIM_SUCCESS) {
        printf("Error getting stats\n");
    } else {
        printf("Got stats\n");
    }

    // Get the stats for the secondary index so the client figures out who to query
    ret = mdhimStatFlush(md, secondary_local_index3);
    if(ret != MDHIM_SUCCESS) {
        printf("Error getting stats\n");
    } else {
        printf("Got stats\n");
    }

    secondary_local_index_by_name = NULL;
 
    MPI_Barrier(MPI_COMM_WORLD);

    printf("[RANK %d] - Attempting to get secondary_local_index '%s'\n", md->mdhim_rank, secondary_local_index->name);
    secondary_local_index_by_name = get_index_by_name(md,secondary_local_index->name);
    if(secondary_local_index_by_name && strcmp(secondary_local_index_by_name->name,secondary_local_index->name)==0) {
        printf("[RANK %d] - Found secondary_local_index %s\n",md->mdhim_rank, secondary_local_index_by_name->name);
    } else {
        printf("[RANK %d] - Error finding secondary_local_index by name '%s'\n", md->mdhim_rank, secondary_local_index->name);
    }

    // Get the primary key values from the secondary local key
    value = 0;
    bgrm = mdhimGet(md, secondary_local_index,
            &secondary_keys[0][0],
            secondary_key_lens[0],
            MDHIM_GET_PRIMARY_EQ);

    if(!bgrm || bgrm->error) {
        printf("Error getting value for key: %d from MDHIM\n", key);
    }else if (bgrm->value_lens[0]) {
        printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
    }

    mdhim_full_release_msg(bgrm);

    printf("[RANK %d] - Attempting to get secondary_local_index '%s'\n", md->mdhim_rank, secondary_local_index2->name);
    secondary_local_index_by_name = get_index_by_name(md,secondary_local_index2->name);
    if(secondary_local_index_by_name && strcmp(secondary_local_index_by_name->name,secondary_local_index2->name)==0) {
        printf("[RANK %d] - Found secondary_local_index %s\n",md->mdhim_rank, secondary_local_index_by_name->name);
    } else {
        printf("[RANK %d] - Error finding secondary_local_index by name '%s'\n", md->mdhim_rank, secondary_local_index2->name);
    }

    // Get the primary key values from the secondary local key
    value = 0;
    bgrm = mdhimGet(md, secondary_local_index,
            &secondary_keys[0][0],
            secondary_key_lens[0],
            MDHIM_GET_PRIMARY_EQ);

    if(!bgrm || bgrm->error) {
        printf("Error getting value for key: %d from MDHIM\n", key);
    }else if (bgrm->value_lens[0]) {
        printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
    }

    mdhim_full_release_msg(bgrm);

    printf("[RANK %d] - Attempting to get secondary_local_index '%s'\n", md->mdhim_rank, secondary_local_index3->name);
    secondary_local_index_by_name = get_index_by_name(md,secondary_local_index3->name);

    if(secondary_local_index_by_name && strcmp(secondary_local_index_by_name->name,secondary_local_index3->name)==0) {
        printf("[RANK %d] - Found secondary_local_index %s\n",md->mdhim_rank, secondary_local_index_by_name->name);
    } else {
        printf("[RANK %d] - Error finding secondary_local_index by name '%s'\n", md->mdhim_rank, secondary_local_index3->name);
    }

    // Get the primary key values from the secondary local key
    value = 0;
    bgrm = mdhimGet(md, secondary_local_index,
            &secondary_keys[0][0],
            secondary_key_lens[0],
            MDHIM_GET_PRIMARY_EQ);

    if(!bgrm || bgrm->error) {
        printf("Error getting value for key: %d from MDHIM\n", key);
    }else if (bgrm->value_lens[0]) {
        printf("Successfully got value: %d from MDHIM\n", *((int *) bgrm->values[0]));
    }

    mdhim_full_release_msg(bgrm);


    ret = mdhimClose(md);
    free(secondary_keys[0]);
    free(secondary_keys);
    free(secondary_key_lens);
    free(secondary_keys2[0]);
    free(secondary_keys2);
    free(secondary_key_lens2);
    free(secondary_keys3[0]);
    free(secondary_keys3);
    free(secondary_key_lens3);
    mdhim_options_destroy(db_opts);
    mdhimReleaseSecondaryInfo(secondary_info);
    mdhimReleaseSecondaryInfo(secondary_info2);
    mdhimReleaseSecondaryInfo(secondary_info3);

    if(ret != MDHIM_SUCCESS) {
        printf("Error closing MDHIM\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();

    return 0;
}
