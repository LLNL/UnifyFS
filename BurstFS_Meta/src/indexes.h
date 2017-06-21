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

#ifndef      __INDEX_H
#define      __INDEX_H

#include "uthash.h"
#include "mdhim_options.h"

#define PRIMARY_INDEX 1
#define SECONDARY_INDEX 2
#define LOCAL_INDEX 3
#define REMOTE_INDEX 4

typedef struct rangesrv_info rangesrv_info;
/* 
 * Range server info  
 * Contains information about each range server
 */
struct rangesrv_info {
	//The range server's rank in the mdhim_comm
	uint32_t rank;
	//The range server's identifier based on rank and number of servers
	uint32_t rangesrv_num;
	uint32_t num_recs;
	void *first_key;

	UT_hash_handle hh;         /* makes this structure hashable */

};

/* 
 * Remote Index info  
 * Contains information about a remote index
 *
 * A remote index means that an index can be served by one or more range servers
 */
struct index_t {
	int id;         // Hash key
	char *name;     // Secondary Hash key

	//The abstracted data store layer that mdhim uses to store and retrieve records
	struct mdhim_store_t *mdhim_store;
	//Options for the mdhim data store
	int key_type;             //The key type used in the db
	int db_type;              //The database type
	int type;                 /* The type of index 
				     (PRIMARY_INDEX, SECONDARY_INDEX, LOCAL_INDEX) */
	int primary_id;           /* The primary index id if this is a secondary index */
	rangesrv_info *rangesrvs_by_num; /* Hash table of the range servers 
					    serving this index.  Key is range server number */
	rangesrv_info *rangesrvs_by_rank; /* Hash table of the range servers 
					     serving this index.  Key is the rank */
        //Used to determine the number of range servers which is based in  
        //if myrank % RANGE_SERVER_FACTOR == 0, then myrank is a server
	int range_server_factor;
	
        //Maximum size of a slice. A range server may serve several slices.
	uint64_t mdhim_max_recs_per_slice; 

	//This communicator is for range servers only to talk to each other
	MPI_Comm rs_comm;   
	/* The rank of the range server master that will broadcast stat data to all clients
	   This rank is the rank in mdhim_comm not in the range server communicator */
	int rangesrv_master;

	//The number of range servers for this index
	uint32_t num_rangesrvs;

	//The rank's range server information, if it is a range server for this index
	rangesrv_info myinfo;

	//Statistics retrieved from the mdhimStatFlush operation
	struct mdhim_stat *stats;

	UT_hash_handle hh;         /* makes this structure hashable */
	UT_hash_handle hh_name;    /* makes this structure hashable by name */
};

typedef struct index_manifest_t {
	int key_type;   //The type of key 
	int index_type; /* The type of index 
			   (PRIMARY_INDEX, SECONDARY_INDEX) */
	int index_id; /* The id of the index in the hash table */
	int primary_id;
	char *index_name; /* The name of the index in the hash table */
	int db_type;
	uint32_t num_rangesrvs;
	int rangesrv_factor;
	uint64_t slice_size; 
	int num_nodes;
	int local_server_rank;
} index_manifest_t;

int update_stat(struct mdhim_t *md, struct index_t *bi, void *key, uint32_t key_len);
int load_stats(struct mdhim_t *md, struct index_t *bi);
int write_stats(struct mdhim_t *md, struct index_t *bi);
int open_db_store(struct mdhim_t *md, struct index_t *index);
uint32_t get_num_range_servers(struct mdhim_t *md, struct index_t *index);
struct index_t *create_local_index(struct mdhim_t *md, int db_type, int key_type, char *index_name);
struct index_t *create_global_index(struct mdhim_t *md, int server_factor, 
				    uint64_t max_recs_per_slice, int db_type, 
				    int key_type, char *index_name);
int get_rangesrvs(struct mdhim_t *md, struct index_t *index);
uint32_t is_range_server(struct mdhim_t *md, int rank, struct index_t *index);
int index_init_comm(struct mdhim_t *md, struct index_t *bi);
int get_stat_flush(struct mdhim_t *md, struct index_t *index);
struct index_t *get_index(struct mdhim_t *md, int index_id);
struct index_t *get_index_by_name(struct mdhim_t *md, char *index_name);
void indexes_release(struct mdhim_t *md);
int im_range_server(struct index_t *index);

#endif
