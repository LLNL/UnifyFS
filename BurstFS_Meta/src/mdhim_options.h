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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

/*
*
* Copyright (c) 2014, Los Alamos National Laboratory
*	All rights reserved.
*
*/

#ifndef      __OPTIONS_H
#define      __OPTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
/* Append option */
#define MDHIM_DB_OVERWRITE 0
#define MDHIM_DB_APPEND 1

// Options for the database (used when opening a MDHIM dataStore)
typedef struct mdhim_options_t {
	// -------------------
	//Directory location of DBs
	char *db_path;
   
	//Multiple paths of DBs
	char **db_paths;
	//Number of paths in db_paths
	int num_paths;

	char *manifest_path;

	//Name of each DB (will be modified by adding "_<RANK>" to create multiple
	// unique DB for each rank server.
	char *db_name;
    
	//Different types of dataStores
	//LEVELDB=1 (from data_store.h)
	int db_type;
    
	//Primary key type
	//MDHIM_INT_KEY, MDHIM_LONG_INT_KEY, MDHIM_FLOAT_KEY, MDHIM_DOUBLE_KEY
	//MDHIM_STRING_KEY, MDHIM_BYTE_KEY
	//(from partitioner.h)
	int db_key_type;

	//Force the creation of a new DB (deleting any previous versions if present)
	int db_create_new;

	//Whether to append a value to an existing key or overwrite the value
	//MDHIM_DB_APPEND to append or MDHIM_DB_OVERWRITE (default)
	int db_value_append;
        
	//DEBUG level
	int debug_level;
        
        //Used to determine the number of range servers which is based in  
        //if myrank % rserver_factor == 0, then myrank is a server.
        // This option is used to set range_server_factor previously a defined var.
        int rserver_factor;
        
        //Maximum size of a slice. A ranger server may server several slices.
        uint64_t max_recs_per_slice; 

	//Number of worker threads per range server
	int num_wthreads;

	//Login Credentials 
	char *db_host;
	char *dbs_host;
	char *db_user;
	char *db_upswd;
	char *dbs_user;
	char *dbs_upswd;


} mdhim_options_t;

struct mdhim_options_t* mdhim_options_init();
void mdhim_options_set_db_path(struct mdhim_options_t* opts, char *path);
void mdhim_options_set_db_paths(struct mdhim_options_t* opts, char **paths, int num_paths);
void mdhim_options_set_db_name(struct mdhim_options_t* opts, char *name);
void mdhim_options_set_db_type(struct mdhim_options_t* opts, int type);
void mdhim_options_set_key_type(struct mdhim_options_t* opts, int key_type);
void mdhim_options_set_create_new_db(struct mdhim_options_t* opts, int create_new);
void mdhim_options_set_login_c(struct mdhim_options_t* opts, char* db_hl, char *db_ln, char *db_pw, char *dbs_hl, char *dbs_ln, char *dbs_pw);
void mdhim_options_set_debug_level(struct mdhim_options_t* opts, int dbug);
void mdhim_options_set_value_append(struct mdhim_options_t* opts, int append);
void mdhim_options_set_server_factor(struct mdhim_options_t* opts, int server_factor);
void mdhim_options_set_max_recs_per_slice(struct mdhim_options_t* opts, uint64_t max_recs_per_slice);
void mdhim_options_set_num_worker_threads(struct mdhim_options_t* opts, int num_wthreads);
void set_manifest_path(mdhim_options_t* opts, char *path);
void mdhim_options_destroy(struct mdhim_options_t *opts);
#ifdef __cplusplus
}
#endif
#endif
