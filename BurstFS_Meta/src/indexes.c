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

#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include "mdhim.h"
#include "indexes.h"

struct timeval hashopstart, hashopend;
double hashoptime=0;

struct timeval cmpstart, cmpend;
double cmptime=0;

struct timeval metastart, metaend;
double metatime=0;

struct timeval sleepstart, sleepend;
double sleeptime=0;
/**
 * to_lower
 * convert strings to all lower case
 *
 */
void to_lower(size_t in_length, char *in, char *out) {
    memset(out, 0, in_length);

    // Make sure that the name passed is lowercase
    int i=0;
    for(i=0; i < in_length; i++) {
        out[i] = tolower(in[i]);
    }
}

/**
 * im_range_server
 * checks if I'm a range server
 *
 * @param md  Pointer to the main MDHIM structure
 * @return 0 if false, 1 if true
 */

int im_range_server(struct index_t *index) {
	if (index->myinfo.rangesrv_num > 0) {
		return 1;
	}
	
	return 0;
}

/**
 * open_manifest
 * Opens the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 * @param flags    Flags to open the file with
 */
int open_manifest(struct mdhim_t *md, struct index_t *index, int flags) {
	int fd;	
	char path[PATH_MAX];

	sprintf(path, "%s%d_%d_%d", md->db_opts->manifest_path, index->type, 
		index->id, md->mdhim_rank);
	fd = open(path, flags, 00600);
	if (fd < 0) {
		mlog(MDHIM_SERVER_DBG, "Rank: %d - Error opening manifest file", 
		     md->mdhim_rank);
	}
	
	return fd;
}

/**
 * write_manifest
 * Writes out the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 */
void write_manifest(struct mdhim_t *md, struct index_t *index) {
	index_manifest_t manifest;
	int fd;
	int ret;

	//Range server with range server number 1, for the primary index, is in charge of the manifest
	if (index->type != LOCAL_INDEX && 
	    (index->myinfo.rangesrv_num != 1)) {	
		return;
	} 

	if ((fd = open_manifest(md, index, O_RDWR | O_CREAT | O_TRUNC)) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error opening manifest file", 
		     md->mdhim_rank);
		return;
	}
	
	//Populate the manifest structure
	manifest.num_rangesrvs = index->num_rangesrvs;
	manifest.key_type = index->key_type;
	manifest.db_type = index->db_type;
	manifest.rangesrv_factor = index->range_server_factor;
	manifest.slice_size = index->mdhim_max_recs_per_slice;
	manifest.num_nodes = md->mdhim_comm_size;       
	
	if ((ret = write(fd, &manifest, sizeof(manifest))) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error writing manifest file", 
		     md->mdhim_rank);
	}

	close(fd);
}

/**
 * read_manifest
 * Reads in and validates the manifest file
 *
 * @param md       Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int read_manifest(struct mdhim_t *md, struct index_t *index) {
	int fd;
	int ret;
	index_manifest_t manifest;

	if ((fd = open_manifest(md, index, O_RDWR)) < 0) {
		mlog(MDHIM_SERVER_DBG, "Rank: %d - Couldn't open manifest file", 
		     md->mdhim_rank);
		return MDHIM_SUCCESS;
	}

	if ((ret = read(fd, &manifest, sizeof(manifest))) < 0) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - Couldn't read manifest file", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	ret = MDHIM_SUCCESS;	
	mlog(MDHIM_SERVER_DBG, "Rank: %d - Manifest contents - \nnum_rangesrvs: %d, key_type: %d, " 
	     "db_type: %d, rs_factor: %u, slice_size: %lu, num_nodes: %d", 
	     md->mdhim_rank, manifest.num_rangesrvs, manifest.key_type, manifest.db_type, 
	     manifest.rangesrv_factor, manifest.slice_size, manifest.num_nodes);
	
	//Check that the manifest and the current config match
	if (manifest.key_type != index->key_type) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The key type in the manifest file" 
		     " doesn't match the current key type", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}
	if (manifest.db_type != index->db_type) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The database type in the manifest file" 
		     " doesn't match the current database type", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}

	if (manifest.rangesrv_factor != index->range_server_factor) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The range server factor in the manifest file" 
		     " doesn't match the current range server factor", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}
	if (manifest.slice_size != index->mdhim_max_recs_per_slice) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The slice size in the manifest file" 
		     " doesn't match the current slice size", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}
	if (manifest.num_nodes != md->mdhim_comm_size) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - The number of nodes in this MDHIM instance" 
		     " doesn't match the number used previously", 
		     md->mdhim_rank);
		ret = MDHIM_ERROR;
	}
	
	close(fd);
	return ret;
}

/**
 * update_stat
 * Adds or updates the given stat to the hash table
 *
 * @param md       pointer to the main MDHIM structure
 * @param key      pointer to the key we are examining
 * @param key_len  the key's length
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int update_stat(struct mdhim_t *md, struct index_t *index, void *key, uint32_t key_len) {
	int slice_num;
	void *val1, *val2;
	int float_type = 0;
	struct mdhim_stat *os, *stat;

	//Acquire the lock to update the stats
	gettimeofday(&sleepstart, NULL);
	while (pthread_rwlock_wrlock(index->mdhim_store->mdhim_store_stats_lock) == EBUSY) {
		usleep(10);
	}
	gettimeofday(&sleepend, NULL);
	sleeptime += 1000000*(sleepend.tv_sec-sleepstart.tv_sec)+sleepend.tv_usec-sleepstart.tv_usec;

			gettimeofday(&metastart, NULL);
	if ((float_type = is_float_key(index->key_type)) == 1) {
		val1 = (void *) malloc(sizeof(long));
		val2 = (void *) malloc(sizeof(long));
		/* printf("is float key\n");
		   fflush(stdout);
		*/
	} else if (index->key_type != MDHIM_BURSTFS_KEY){
		val1 = (void *) malloc(sizeof(uint64_t));
		val2 = (void *) malloc(sizeof(uint64_t));
		/* printf("is not burstfs key\n");
		   fflush(stdout);
		   */
	}
	else {
		val1 = NULL;
		val2 = NULL;
		/*
		printf("is burstfs key\n");
		fflush(stdout);
		*/
	}
	/*
	printf("key_type is %d\n", index->key_type);
	fflush(stdout);
	*/
	if (index->key_type == MDHIM_STRING_KEY) {
		*(long double *)val1 = get_str_num(key, key_len);
		*(long double *)val2 = *(long double *)val1;
	} else if (index->key_type == MDHIM_FLOAT_KEY) {
		*(long double *)val1 = *(float *) key;
		*(long double *)val2 = *(float *) key;
	} else if (index->key_type == MDHIM_DOUBLE_KEY) {
		*(long double *)val1 = *(double *) key;
		*(long double *)val2 = *(double *) key;
	} else if (index->key_type == MDHIM_INT_KEY) {
		*(uint64_t *)val1 = *(uint32_t *) key;
		*(uint64_t *)val2 = *(uint32_t *) key;
	} else if (index->key_type == MDHIM_LONG_INT_KEY) {
		*(uint64_t *)val1 = *(uint64_t *) key;
		*(uint64_t *)val2 = *(uint64_t *) key;
	} else if (index->key_type == MDHIM_BYTE_KEY) {
		*(unsigned long  *)val1 = get_byte_num(key, key_len);
		*(unsigned long  *)val2 = *(unsigned long *)val1;
	} else if (index->key_type == MDHIM_BURSTFS_KEY) {
			val1 = get_meta_pair(key, key_len);
			val2 = get_meta_pair(key, key_len);
	}
			gettimeofday(&metaend, NULL);
			metatime+=1000000*(metaend.tv_sec-metastart.tv_sec)+metaend.tv_usec-metastart.tv_usec;

	slice_num = get_slice_num(md, index, key, key_len);
	gettimeofday(&hashopstart, NULL);
	HASH_FIND_INT(index->mdhim_store->mdhim_store_stats, &slice_num, os);
	gettimeofday(&hashopend, NULL);


	// printf("val1 is %ld, val2 is %ld\n", *((long *)val1+1), *((long *)val2+1));
	stat = malloc(sizeof(struct mdhim_stat));
	stat->min = val1;
	stat->max = val2;
	stat->num = 1;
	stat->key = slice_num;
	stat->dirty = 1;
	hashoptime += 1000000*(hashopend.tv_sec-hashopstart.tv_sec)+hashopend.tv_usec-hashopstart.tv_usec;
//	printf("here min stat is %ld, max stat is %ld\n", *((long *)stat->min), *((long *)stat->max));
//	fflush(stdout);

	gettimeofday(&cmpstart, NULL);
	if (index->key_type == MDHIM_BURSTFS_KEY && os ) {
		if (burstfs_compare(os->min, val1) > 0) {
			/*
			printf("freeing %x, va1 addr is %x\n", os->min, val1);
			fflush(stdout);
			*/
			free(os->min);
			stat->min = val1;
		} else {
			stat->min = os->min;
			free(val1);
		}

		if (burstfs_compare(os->max, val2) < 0) {
			/*
			printf("freeing %xb,bbb\n", os->max);
			fflush(stdout);
			*/
			free(os->max);
			stat->max = val2;
		} else {
			stat->max = os->max;
			free(val2);
		}
	}
	gettimeofday(&cmpend, NULL);
	cmptime+=1000000*(cmpend.tv_sec-cmpstart.tv_sec)+cmpend.tv_usec-cmpstart.tv_usec;

	if (float_type && os) {
//		printf("comparing min is %ld, val1 is %ld\n", *((long *)stat->min), *((long *)val1));
//		fflush(stdout);
		if (*(unsigned long *)os->min > *(unsigned long *)val1) {
			/*
			printf("freeing %x 1\n", os->min);
			fflush(stdout);
			*/
			free(os->min);
			stat->min = val1;
		} else {
			/*
			printf("freeing %x 1\n", val1);
			fflush(stdout);
			*/
			free(val1);
			stat->min = os->min;
		}

		if (*(unsigned long *)os->max < *(unsigned long *)val2) {
			/*
			printf("freeing %x2\n", os->max);
			fflush(stdout);
			*/
			free(os->max);
			stat->max = val2;
		} else {
			/*
			printf("freeing %x2\n", val2);
			fflush(stdout);
			*/
			free(val2);
			stat->max = os->max;
		}
	}	

	if (!float_type && os && index->key_type != MDHIM_BURSTFS_KEY) {
		if (*(uint64_t *)os->min > *(uint64_t *)val1) {
			/*
			printf("freeing %x3\n", os->min);
			fflush(stdout);
			*/
			free(os->min);
			stat->min = val1;
		} else {
			/*
			printf("freeing %x3\n", val1);
			fflush(stdout);
			*/
			free(val1);
			stat->min = os->min;
		}

		if (*(uint64_t *)os->max < *(uint64_t *)val2) {
			/*
			printf("freeing %x4\n", os->max);
			fflush(stdout);
			*/
			free(os->max);
			stat->max = val2;
		} else {
			/*
			printf("freeing %x4\n", val2);
			fflush(stdout);
			*/
			free(val2);
			stat->max = os->max;
		}		
	}

	if (!os) {
		gettimeofday(&hashopstart, NULL);
		HASH_ADD_INT(index->mdhim_store->mdhim_store_stats, key, stat);    
		gettimeofday(&hashopend, NULL);
		hashoptime += 1000000*(hashopend.tv_sec-hashopstart.tv_sec)+hashopend.tv_usec-hashopstart.tv_usec;
	} else {	
		stat->num = os->num + 1;

		//Replace the existing stat
		gettimeofday(&hashopstart, NULL);
		HASH_REPLACE_INT(index->mdhim_store->mdhim_store_stats, key, stat, os);  
		free(os);
		gettimeofday(&hashopend, NULL);
		hashoptime += 1000000*(hashopend.tv_sec-hashopstart.tv_sec)+hashopend.tv_usec-hashopstart.tv_usec;
	}

	//Release the stats lock
	gettimeofday(&sleepstart, NULL);
	pthread_rwlock_unlock(index->mdhim_store->mdhim_store_stats_lock);
	gettimeofday(&sleepend, NULL);
	sleeptime +=1000000*(sleepend.tv_sec-sleepstart.tv_sec)+sleepend.tv_usec-sleepstart.tv_usec;
	return MDHIM_SUCCESS;
}

/**
 * load_stats
 * Loads the statistics from the database
 *
 * @param md  Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int load_stats(struct mdhim_t *md, struct index_t *index) {
	void **val;
	int *val_len, *key_len;
	int **slice;
	int *old_slice;
	struct mdhim_stat *stat;
	int float_type = 0;
	void *min, *max;
	int done = 0;

	float_type = is_float_key(index->key_type);
	slice = malloc(sizeof(int *));
	*slice = NULL;
	key_len = malloc(sizeof(int));
	*key_len = sizeof(int);
	val = malloc(sizeof(struct mdhim_db_stat *));	
	val_len = malloc(sizeof(int));
	old_slice = NULL;
	index->mdhim_store->mdhim_store_stats = NULL;
	while (!done) {
		//Check the db for the key/value
		*val = NULL;
		*val_len = 0;		
		index->mdhim_store->get_next(index->mdhim_store->db_stats, 
					     (void **) slice, key_len, (void **) val, 
					     val_len);	
		
		//Add the stat to the hash table - the value is 0 if the key was not in the db
		if (!*val || !*val_len) {
			done = 1;
			continue;
		}

		if (old_slice) {
			free(old_slice);
			old_slice = NULL;
		}

		mlog(MDHIM_SERVER_DBG, "Rank: %d - Loaded stat for slice: %d with " 
		     "imin: %lu and imax: %lu, dmin: %Lf, dmax: %Lf, and num: %lu", 
		     md->mdhim_rank, **slice, (*(struct mdhim_db_stat **)val)->imin, 
		     (*(struct mdhim_db_stat **)val)->imax, (*(struct mdhim_db_stat **)val)->dmin, 
		     (*(struct mdhim_db_stat **)val)->dmax, (*(struct mdhim_db_stat **)val)->num);
	
		stat = malloc(sizeof(struct mdhim_stat));
		if (float_type) {
			min = (void *) malloc(sizeof(long double));
			max = (void *) malloc(sizeof(long double));
			*(long double *)min = (*(struct mdhim_db_stat **)val)->dmin;
			*(long double *)max = (*(struct mdhim_db_stat **)val)->dmax;
		} else {
			min = (void *) malloc(sizeof(uint64_t));
			max = (void *) malloc(sizeof(uint64_t));
			*(uint64_t *)min = (*(struct mdhim_db_stat **)val)->imin;
			*(uint64_t *)max = (*(struct mdhim_db_stat **)val)->imax;
		}

		stat->min = min;
		stat->max = max;
		stat->num = (*(struct mdhim_db_stat **)val)->num;
		stat->key = **slice;
		stat->dirty = 0;
		old_slice = *slice;
		HASH_ADD_INT(index->mdhim_store->mdhim_store_stats, key, stat); 
		free(*val);
	}

	if (old_slice) {
		free(old_slice);
	}
	free(val);
	free(val_len);
	free(key_len);
	free(*slice);
	free(slice);
	return MDHIM_SUCCESS;
}

/**
 * write_stats
 * Writes the statistics stored in a hash table to the database
 * This is done on mdhim_close
 *
 * @param md  Pointer to the main MDHIM structure
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int write_stats(struct mdhim_t *md, struct index_t *bi) {
	struct mdhim_stat *stat, *tmp;
	struct mdhim_db_stat *dbstat;
	int float_type = 0;

	float_type = is_float_key(bi->key_type);

	//Iterate through the stat hash entries
	HASH_ITER(hh, bi->mdhim_store->mdhim_store_stats, stat, tmp) {	
		if (!stat) {
			continue;
		}

		if (!stat->dirty) {
			goto free_stat;
		}

		dbstat = malloc(sizeof(struct mdhim_db_stat));
		if (float_type) {
			dbstat->dmax = *(long double *)stat->max;
			dbstat->dmin = *(long double *)stat->min;
			dbstat->imax = 0;
			dbstat->imin = 0;
		} else {
			dbstat->imax = *(uint64_t *)stat->max;
			dbstat->imin = *(uint64_t *)stat->min;
			dbstat->dmax = 0;
			dbstat->dmin = 0;
		}

		dbstat->slice = stat->key;
		dbstat->num = stat->num;
		//Write the key to the database		
		bi->mdhim_store->put(bi->mdhim_store->db_stats, 
				     &dbstat->slice, sizeof(int), dbstat, 
				     sizeof(struct mdhim_db_stat));	
		//Delete and free hash entry
		free(dbstat);

	free_stat:
		HASH_DEL(bi->mdhim_store->mdhim_store_stats, stat); 
		free(stat->max);
		free(stat->min);
		free(stat);
	}

	return MDHIM_SUCCESS;
}

/** 
 * open_db_store
 * Opens the database for the given idenx
 *
 * @param md     Pointer to the main MDHIM structure
 * @param index Pointer to the index
 * @return the initialized data store or NULL on error
 */

int open_db_store(struct mdhim_t *md, struct index_t *index) {
	char filename[PATH_MAX] = {'\0'};
	int flags = MDHIM_CREATE;
	int path_num;
	int ret;

	//Database filename is dependent on ranges.  This needs to be configurable and take a prefix
	if (!md->db_opts->db_paths) {
		sprintf(filename, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name, 
			index->id, md->mdhim_rank);
	} else {
		path_num = index->myinfo.rangesrv_num/((double) index->num_rangesrvs/(double) md->db_opts->num_paths);
		path_num = path_num >= md->db_opts->num_paths ? md->db_opts->num_paths - 1 : path_num;
		if (path_num < 0) {
			sprintf(filename, "%s%s-%d-%d", md->db_opts->db_path, md->db_opts->db_name, index->id, 
				md->mdhim_rank);
		} else {
			sprintf(filename, "%s%s-%d-%d", md->db_opts->db_paths[path_num], 
				md->db_opts->db_name, index->id, md->mdhim_rank);
		}
	}

	//Initialize data store
	index->mdhim_store = mdhim_db_init(index->db_type);
	if (!index->mdhim_store) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while initializing data store with file: %s",
		     md->mdhim_rank,
		     filename);
		return MDHIM_ERROR;
	}

	//Open the main database and the stats database
	if ((ret = index->mdhim_store->open(&index->mdhim_store->db_handle,
					    &index->mdhim_store->db_stats,
					    filename, flags, index->key_type, md->db_opts)) != MDHIM_SUCCESS){
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while opening database", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	
	//Load the stats from the database
	if ((ret = load_stats(md, index)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while loading stats", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	return MDHIM_SUCCESS;
}

/**
 * get_num_range_servers
 * Gets the number of range servers for an index
 *
 * @param md       main MDHIM struct
 * @param rindex   pointer to a index_t struct
 * @return         MDHIM_ERROR on error, otherwise the number of range servers
 */
uint32_t get_num_range_servers(struct mdhim_t *md, struct index_t *rindex) {
	int size;
	uint32_t num_servers = 0;
	int i = 0;
	int ret;

	if ((ret = MPI_Comm_size(md->mdhim_comm, &size)) != MPI_SUCCESS) {
		mlog(MPI_EMERG, "Rank: %d - Couldn't get the size of the comm in get_num_range_servers", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	/* Get the number of range servers */
	if (size - 1 < rindex->range_server_factor) {
		//The size of the communicator is less than the RANGE_SERVER_FACTOR
		return 1;
	} 
	
	//Figure out the number of range servers, details on the algorithm are in is_range_server
	for (i = 0; i < size; i++) {
		if (i % rindex->range_server_factor == 0) {
			num_servers++;
		}
	}
      	
	return num_servers;
}

/**
 * create_local_index
 * Creates an index on the primary index that is handled by the same servers as the primary index.
 * This index does not have global ordering.  Ordering is local to the range server only.
 * Retrieving a key from this index will require querying multiple range servers simultaneously.
 *
 * @param  md  main MDHIM struct
 * @return     MDHIM_ERROR on error, otherwise the index identifier
 */
struct index_t *create_local_index(struct mdhim_t *md, int db_type, int key_type, char *index_name) {
	struct index_t *li;
	struct index_t *check = NULL;
	uint32_t rangesrv_num;
	int ret;

	MPI_Barrier(md->mdhim_client_comm);

	//Check that the key type makes sense
	if (key_type < MDHIM_INT_KEY || key_type > MDHIM_BURSTFS_KEY) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM - Invalid key type specified");
		return NULL;
	}

	//Acquire the lock to update indexes
	while (pthread_rwlock_wrlock(md->indexes_lock) == EBUSY) {
		usleep(10);
	}		

	//Create a new global_index to hold our index entry
	li = malloc(sizeof(struct index_t));
	if (!li) {
		goto done;
	}

	//Initialize the new index struct
	memset(li, 0, sizeof(struct index_t));
	li->id = HASH_COUNT(md->indexes);
	li->range_server_factor = md->primary_index->range_server_factor;
	li->mdhim_max_recs_per_slice = MDHIM_MAX_SLICES;
	li->type = LOCAL_INDEX;
	li->key_type = key_type;
	li->db_type = db_type;
	li->myinfo.rangesrv_num = 0;
	li->myinfo.rank = md->mdhim_rank;
	li->primary_id = md->primary_index->id;
	li->stats = NULL;

    if (index_name != NULL) {
        size_t name_len = strlen(index_name)+1;
        char *lower_name = malloc(name_len);

        to_lower(name_len, index_name, lower_name);

        // check if the name has been used
        HASH_FIND_STR(md->indexes, lower_name, check);
        if(check) {
            goto done;
        }

        li->name = malloc(name_len);
        memcpy(li->name, lower_name, name_len);

    } else {
        char buf[50];
        sprintf(buf, "local_%d", li->id);
        li->name = malloc(sizeof(char)*strlen(buf));
        strcpy(li->name, buf);
    }


	//Figure out how many range servers we could have based on the range server factor
	li->num_rangesrvs = get_num_range_servers(md, li);		

	//Get the range servers for this index
	ret = get_rangesrvs(md, li);
	if (ret != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Couldn't get the range server list", 
		     md->mdhim_rank);
	}

	//Add it to the hash table
	HASH_ADD_INT(md->indexes, id, li);
	HASH_ADD_KEYPTR( hh_name, md->indexes_by_name, li->name, strlen(li->name), li );

	//Test if I'm a range server and get the range server number
	if ((rangesrv_num = is_range_server(md, md->mdhim_rank, li)) == MDHIM_ERROR) {	
		goto done;
	}

	if (rangesrv_num > 0) {
		//Populate my range server info for this index
		li->myinfo.rank = md->mdhim_rank;
		li->myinfo.rangesrv_num = rangesrv_num;
	}

	//If not a range server, our work here is done
	if (!rangesrv_num) {
		goto done;
	}	

	//Read in the manifest file if the rangesrv_num is 1 for the primary index
	if (rangesrv_num == 1 && 
	    (ret = read_manifest(md, li)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error: There was a problem reading or validating the manifest file",
		     md->mdhim_rank);
		MPI_Abort(md->mdhim_comm, 0);
	}        

	//Open the data store
	ret = open_db_store(md, (struct index_t *) li);
	if (ret != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error opening data store for index: %d", 
		     md->mdhim_rank, li->id);
		MPI_Abort(md->mdhim_comm, 0);
	}

	//Initialize the range server threads if they haven't been already
	if (!md->mdhim_rs) {
		ret = range_server_init(md);
	}
	
done:
	//Release the indexes lock
	if (pthread_rwlock_unlock(md->indexes_lock) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error unlocking the indexes_lock", 
		     md->mdhim_rank);
		return NULL;
	}

	if (!li) {
		return NULL;
	}

	// The index name has already been taken
	if(check) {
        mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error creating index: Name %s, already exists", md->mdhim_rank, index_name);
        return NULL;
    }

	return li;
}

/**
 * create_global_index
 * Collective call that creates a global index.
 * A global index has global ordering.  This means that range servers serve mutually exclusive keys
 * and keys can be retrieved across servers in order.  Retrieving a key will query only one range
 * server.
 *
 * @param md                 main MDHIM struct
 * @param server_factor      used in calculating the number of range servers
 * @param max_recs_per_slice the number of records per slice
 * @return                   MDHIM_ERROR on error, otherwise the index identifier
 */

struct index_t *create_global_index(struct mdhim_t *md, int server_factor, 
				    uint64_t max_recs_per_slice, 
				    int db_type, int key_type, char *index_name) {
	struct index_t *gi;
	struct index_t *check = NULL;
	uint32_t rangesrv_num;
	int ret;

	MPI_Barrier(md->mdhim_client_comm);
	//Check that the key type makes sense
	if (key_type < MDHIM_INT_KEY || key_type > MDHIM_BURSTFS_KEY) {
		mlog(MDHIM_CLIENT_CRIT, "MDHIM - Invalid key type specified");
		return NULL;
	}

	//Acquire the lock to update indexes
	while (pthread_rwlock_wrlock(md->indexes_lock) == EBUSY) {
		usleep(10);
	}		

	//Create a new global_index to hold our index entry
	gi = malloc(sizeof(struct index_t));
	if (!gi) {
		goto done;
	}

	//Initialize the new index struct
	memset(gi, 0, sizeof(struct index_t));
	gi->id = HASH_COUNT(md->indexes);
	gi->range_server_factor = server_factor;
	gi->mdhim_max_recs_per_slice = max_recs_per_slice;
	gi->type = gi->id > 0 ? SECONDARY_INDEX : PRIMARY_INDEX;
	gi->key_type = key_type;
	gi->db_type = db_type;
	gi->myinfo.rangesrv_num = 0;
	gi->myinfo.rank = md->mdhim_rank;
	gi->primary_id = gi->type == SECONDARY_INDEX ? md->primary_index->id : -1;
	gi->stats = NULL;

    if (gi->id > 0) {

        if (index_name != NULL) {

            size_t name_len = strlen(index_name)+1;
            char *lower_name = malloc(name_len);

            to_lower(name_len, index_name, lower_name);

            // check if the name has been used
            HASH_FIND_STR(md->indexes, lower_name, check);
            if(check) {
                goto done;
            }

            gi->name = malloc(name_len);
            memcpy(gi->name, lower_name, name_len);

        } else {
            char buf[50];
            sprintf(buf, "global_%d", gi->id);
            gi->name = malloc(sizeof(char)*strlen(buf));
            strcpy(gi->name, buf);
        }

    } else {
        gi->name = malloc(sizeof(char)*10);
        strcpy(gi->name, "primary");
    }

	//Figure out how many range servers we could have based on the range server factor
	gi->num_rangesrvs = get_num_range_servers(md, gi);		
	//Get the range servers for this index
	ret = get_rangesrvs(md, gi);
	if (ret != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Couldn't get the range server list", 
		     md->mdhim_rank);
	}

	//Add it to the hash table
	HASH_ADD_INT(md->indexes, id, gi);
	HASH_ADD_KEYPTR( hh_name, md->indexes_by_name, gi->name, strlen(gi->name), gi );

	//Test if I'm a range server and get the range server number
	if ((rangesrv_num = is_range_server(md, md->mdhim_rank, gi)) == MDHIM_ERROR) {	
		goto done;
	}

	if (rangesrv_num > 0) {
		//Populate my range server info for this index
		gi->myinfo.rank = md->mdhim_rank;
		gi->myinfo.rangesrv_num = rangesrv_num;
	}

	//Initialize the communicator for this index
	if ((ret = index_init_comm(md, gi)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error creating the index communicator", 
		     md->mdhim_rank);
		goto done;
	}
	//If not a range server, our work here is done
	if (!rangesrv_num) {
		goto done;
	}	

	//Read in the manifest file if the rangesrv_num is 1 for the primary index
	if (rangesrv_num == 1 && 
	    (ret = read_manifest(md, gi)) != MDHIM_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error: There was a problem reading or validating the manifest file",
		     md->mdhim_rank);
		MPI_Abort(md->mdhim_comm, 0);
	}        

	//Open the data store
	ret = open_db_store(md, (struct index_t *) gi);
	if (ret != MDHIM_SUCCESS) {
	//	printf("open store failed with error %d, rank%d\n", ret, md->mdhim_rank);
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error opening data store for index: %d", 
		     md->mdhim_rank, gi->id);
	}

	//Initialize the range server threads if they haven't been already

	if (!md->mdhim_rs) {
		ret = range_server_init(md);
	}

done:
	//Release the indexes lock
	if (pthread_rwlock_unlock(md->indexes_lock) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error unlocking the indexes_lock", 
		     md->mdhim_rank);
		return NULL;
	}

	if (!gi) {
		return NULL;
	}

	// The index name has already been taken
	if(check) {
        mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error creating index: Name %s, already exists", md->mdhim_rank, index_name);
        return NULL;
    }

	return gi;
}

/**
 * get_rangesrvs
 * Creates a rangesrv_info hash table 
 *
 * @param md      in   main MDHIM struct
 * @return a list of range servers
 */
int get_rangesrvs(struct mdhim_t *md, struct index_t *index) {
	struct rangesrv_info *rs_entry_num, *rs_entry_rank;
	uint32_t rangesrv_num;
	int i;

	//Iterate through the ranks to determine which ones are range servers
	for (i = 0; i < md->mdhim_comm_size; i++) {
		//Test if the rank is range server for this index
		if ((rangesrv_num = is_range_server(md, i, index)) == MDHIM_ERROR) {
			continue;
		}

		if (!rangesrv_num) {
			continue;
		}

		//Set the master range server to be the server with the largest rank
		if (i > index->rangesrv_master) {
			index->rangesrv_master = i;
		}

		rs_entry_num = malloc(sizeof(struct rangesrv_info));
		rs_entry_rank = malloc(sizeof(struct rangesrv_info));
		rs_entry_num->rank = rs_entry_rank->rank = i;
		rs_entry_rank->rangesrv_num = rs_entry_num->rangesrv_num = rangesrv_num;
//		printf("range server rank is %d, range server num is %d, myrank is %d\n", rs_entry_rank->rank, rs_entry_num->rangesrv_num, md->mdhim_rank);
//		fflush(stdout);
		//Add it to the hash tables
		HASH_ADD_INT(index->rangesrvs_by_num, rangesrv_num, rs_entry_num);     
		HASH_ADD_INT(index->rangesrvs_by_rank, rank, rs_entry_rank);                 
	}

	return MDHIM_SUCCESS;
}

/**
 * is_range_server
 * Tests to see if the given rank is a range server for one or more indexes
 *
 * @param md      main MDHIM struct
 * @param rank    rank to find out if it is a range server
 * @return        MDHIM_ERROR on error, 0 on false, 1 or greater to represent the range server number otherwise
 */
uint32_t is_range_server(struct mdhim_t *md, int rank, struct index_t *index) {
	int size;
	int ret;
	uint64_t rangesrv_num = 0;

	//If a local index, check to see if the rank is a range server for the primary index
	if (index->type == LOCAL_INDEX) {
		rangesrv_num = is_range_server(md, rank, md->primary_index);

		return rangesrv_num;
	}

	if ((ret = MPI_Comm_size(md->mdhim_comm, &size)) != MPI_SUCCESS) {
		mlog(MPI_EMERG, "Rank: %d - Couldn't get the size of the comm in is_range_server", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	/* Get the range server number, which is just a number from 1 onward
	   It represents the ranges the server serves and is calculated with the RANGE_SERVER_FACTOR
	   
	   The RANGE_SERVER_FACTOR is a number that is divided by the rank such that if the 
	   remainder is zero, then the rank is a rank server

	   For example, if there were 8 ranks and the RANGE_SERVER_FACTOR is 2, 
	   then ranks: 0, 2, 4, 6 are range servers

	   If the size of communicator is less than the RANGE_SERVER_FACTOR, 
	   the last rank is the range server
	*/

	size -= 1;
	if (size < index->range_server_factor && rank == size) {
		//The size of the communicator is less than the RANGE_SERVER_FACTOR
		rangesrv_num = 1;

	} else if (size >= index->range_server_factor && rank % index->range_server_factor == 0) {
		//This is a range server, get the range server's number
		rangesrv_num = rank / index->range_server_factor;
		rangesrv_num++;
	}
      		
	if (rangesrv_num > index->num_rangesrvs) {
		rangesrv_num = 0;
	}
//	printf("rangeser_num is %d, rank is %d\n", rangesrv_num, md->mdhim_rank);
	return rangesrv_num;
}

/**
 * range_server_init_comm
 * Initializes the range server communicator that is used for range server to range 
 * server collectives
 * The stat flush function will use this communicator
 *
 * @param md  Pointer to the main MDHIM structure
 * @return    MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int index_init_comm(struct mdhim_t *md, struct index_t *bi) {
	MPI_Group orig, new_group;
	int *ranks;
	int i = 0;
	int ret;
	int comm_size, size;
	MPI_Comm new_comm;
	struct rangesrv_info *rangesrv, *tmp;

	ranks = NULL;
	size = 0;
	//Populate the ranks array that will be in our new comm
	if ((ret = im_range_server(bi)) == 1) {
		ranks = malloc(sizeof(int) * bi->num_rangesrvs);
		//Iterate through the stat hash entries
		HASH_ITER(hh, bi->rangesrvs_by_rank, rangesrv, tmp) {	
			if (!rangesrv) {
				continue;
			}

			ranks[size] = rangesrv->rank;
			size++;
		}
	} else {
		MPI_Comm_size(md->mdhim_comm, &comm_size);
		ranks = malloc(sizeof(int) * comm_size);
		for (i = 0; i < comm_size; i++) {
			HASH_FIND_INT(bi->rangesrvs_by_rank, &i, rangesrv);
			if (rangesrv) {
				continue;
			}

			ranks[size] = i;
			size++;
		}
	}

	//Create a new group with the range servers only
	if ((ret = MPI_Comm_group(md->mdhim_comm, &orig)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating a new group in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	if ((ret = MPI_Group_incl(orig, size, ranks, &new_group)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating adding ranks to the new group in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	if ((ret = MPI_Comm_create(md->mdhim_comm, new_group, &new_comm)) 
	    != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
		     "Error while creating the new communicator in range_server_init_comm", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}
	if ((ret = im_range_server(bi)) == 1) {
		memcpy(&bi->rs_comm, &new_comm, sizeof(MPI_Comm));
	} else {
		MPI_Comm_free(&new_comm);
	}

	MPI_Group_free(&orig);
	MPI_Group_free(&new_group);
	free(ranks);
	return MDHIM_SUCCESS;
}

struct index_t *get_index(struct mdhim_t *md, int index_id) {
	struct index_t *index;

	//Acquire the lock to update indexes	
	while (pthread_rwlock_wrlock(md->indexes_lock) == EBUSY) {
		usleep(10);
	}		
	
	index = NULL;
	if (index_id >= 0) {
		HASH_FIND(hh, md->indexes, &index_id, sizeof(int), index);
	}

	if (pthread_rwlock_unlock(md->indexes_lock) != 0) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error unlocking the indexes_lock", 
		     md->mdhim_rank);
		return NULL;
	}

	return index;
}


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  get_index_by_name
 *  Description:  Retrieve the index by name
 * =====================================================================================
 */
struct index_t*
get_index_by_name ( struct mdhim_t *md, char *index_name )
{
    struct index_t *index = NULL;
    size_t name_len = strlen(index_name)+1;
    char *lower_name = malloc(name_len);

    // Acquire the lock to update indexes
    while ( pthread_rwlock_wrlock(md->indexes_lock) == EBUSY ) {
        usleep(10);
    }
    
    to_lower(name_len, index_name, lower_name);

    if ( strcmp(lower_name, "") != 0 ) {
        HASH_FIND(hh_name, md->indexes_by_name, lower_name, strlen(lower_name), index);
    }

    if ( pthread_rwlock_unlock(md->indexes_lock) !=0 ) {
        mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error unlocking the indexes_lock",
                md->mdhim_rank);
        return NULL;
    }

return index;
}		/* -----  end of function get_index_by_name  ----- */

void indexes_release(struct mdhim_t *md) {
	struct index_t *cur_indx, *tmp_indx;
	struct rangesrv_info *cur_rs, *tmp_rs;
	int ret;
	struct mdhim_stat *stat, *tmp;

	HASH_ITER(hh, md->indexes, cur_indx, tmp_indx) {
		HASH_DELETE(hh, md->indexes, cur_indx); 
		HASH_DELETE(hh_name, md->indexes_by_name, cur_indx);
		HASH_ITER(hh, cur_indx->rangesrvs_by_num, cur_rs, tmp_rs) {
			HASH_DEL(cur_indx->rangesrvs_by_num, cur_rs); 
			free(cur_rs);
		}

		HASH_ITER(hh, cur_indx->rangesrvs_by_rank, cur_rs, tmp_rs) {
			HASH_DEL(cur_indx->rangesrvs_by_rank, cur_rs); 
			free(cur_rs);
		}
		
		//Clean up the storage if I'm a range server for this index
		if (cur_indx->myinfo.rangesrv_num > 0) {
			//Write the stats to the database
			if ((ret = write_stats(md, cur_indx)) != MDHIM_SUCCESS) {
				mlog(MDHIM_SERVER_CRIT, "MDHIM Rank: %d - " 
				     "Error while loading stats", 
				     md->mdhim_rank);
			}

			if (cur_indx->myinfo.rangesrv_num == 1) {
				//Write the manifest
				write_manifest(md, cur_indx);
			}
			
			//Close the database
			if ((ret = cur_indx->mdhim_store->close(cur_indx->mdhim_store->db_handle, 
								cur_indx->mdhim_store->db_stats)) 
			    != MDHIM_SUCCESS) {
				mlog(MDHIM_SERVER_CRIT, "Rank: %d - Error closing database", 
				     md->mdhim_rank);
			}
			
			pthread_rwlock_destroy(cur_indx->mdhim_store->mdhim_store_stats_lock);
			free(cur_indx->mdhim_store->mdhim_store_stats_lock);
			if (cur_indx->type != LOCAL_INDEX) {
				MPI_Comm_free(&cur_indx->rs_comm);
			}
			free(cur_indx->mdhim_store);
		}
	
		
		//Iterate through the stat hash entries to free them
		HASH_ITER(hh, cur_indx->stats, stat, tmp) {	
			if (!stat) {
				continue;
			}
			
			HASH_DEL(cur_indx->stats, stat); 
			free(stat->max);
			free(stat->min);
			free(stat);
		}			
	
		free(cur_indx);
	}
}

int pack_stats(struct index_t *index, void *buf, int size, 
	       int float_type, int stat_size, MPI_Comm comm) {
	
	struct mdhim_stat *stat, *tmp;
	void *tstat;
	struct mdhim_db_istat *istat;
	struct mdhim_db_fstat *fstat;
	int ret = MPI_SUCCESS;
	int sendidx = 0;

	//Pack the stat data I have by iterating through the stats hash
	HASH_ITER(hh, index->mdhim_store->mdhim_store_stats, stat, tmp) {
		//Get the appropriate struct to send
		if (float_type) {
			fstat = malloc(sizeof(struct mdhim_db_fstat));
			fstat->slice = stat->key;
			fstat->num = stat->num;
		
			if (index->key_type == MDHIM_BURSTFS_KEY) {
/*				printf("bbbefore:min fid is %ld, min offset is %ld, \ 
					max fid is %ld, max offset is %ld\n", \
					*((ulong *)stat->min), *((ulong *)stat->min+1), \
					*((ulong *)stat->max), *((ulong *)stat->max+1));
				fflush(stdout);
*/
				memcpy((char *)&fstat->dmin, (char *)stat->min, sizeof(ulong));
				memcpy((char *)(&fstat->dmin)+sizeof(ulong), (char *)(stat->min)+sizeof(ulong), \
						sizeof(ulong));
				memcpy((char *)&fstat->dmax, (char *)stat->max, sizeof(ulong));
				memcpy((char *)(&fstat->dmax)+sizeof(ulong), (char *)(stat->max)+sizeof(ulong), \
						sizeof(ulong));				
/*				printf("before:min fid is %ld, min offset is %ld, \ 
					max fid is %ld, max offset is %ld\n", \
					*((ulong *)&(fstat->dmin)), *((ulong *)&(fstat->dmin)+1), \
					*((ulong *)&(fstat->dmax)), *((ulong *)&(fstat->dmax)+1));
				fflush(stdout);
*/	
			}
			else {
				fstat->dmin = *(long double *) stat->min;
				fstat->dmax = *(long double *) stat->max;
			}
				tstat = fstat;
		} else {
			istat = malloc(sizeof(struct mdhim_db_istat));
			istat->slice = stat->key;
			istat->num = stat->num;
			istat->imin = *(uint64_t *) stat->min;
			istat->imax = *(uint64_t *) stat->max;
			tstat = istat;
		}
		  
		//Pack the struct
		if ((ret = MPI_Pack(tstat, stat_size, MPI_CHAR, buf, size, &sendidx, 
				    comm)) != MPI_SUCCESS) {
			mlog(MPI_CRIT, "Error packing buffer when sending stat info" 
			     " to master range server");
			free(buf);
			free(tstat);
			return ret;
		}

		free(tstat);
	}

	return ret;
}

int get_stat_flush_global(struct mdhim_t *md, struct index_t *index) {
	char *sendbuf;
	int sendsize = 0;
	int recvidx = 0;
	char *recvbuf;
	int *recvcounts;
	int *displs;
	int recvsize;
	int ret = 0;
	int i = 0;
	int float_type = 0;
	struct mdhim_stat *stat, *tmp;
	void *tstat;
	int stat_size = 0;
	int master;
	int num_items = 0;
	
	//Determine the size of the buffers to send based on the number and type of stats
	if ((ret = is_float_key(index->key_type)) == 1 || \
			index->key_type == MDHIM_BURSTFS_KEY) {
		float_type = 1;
		stat_size = sizeof(struct mdhim_db_fstat);
	} else {
		float_type = 0;
		stat_size = sizeof(struct mdhim_db_istat);
	}
	recvbuf = NULL;
	if (index->myinfo.rangesrv_num > 0) {
		//Get the number stats in our hash table
		if (index->mdhim_store->mdhim_store_stats) {
			num_items = HASH_COUNT(index->mdhim_store->mdhim_store_stats);
			// printf("num_items is %ld\n", num_items);
			// fflush(stdout);
		} else {
			num_items = 0;
		}
		if ((ret = is_float_key(index->key_type)) == 1 || \ 
				index->key_type == MDHIM_BURSTFS_KEY) {
			sendsize = num_items * sizeof(struct mdhim_db_fstat);
		} else {
			sendsize = num_items * sizeof(struct mdhim_db_istat);
		}
	}

	if (index->myinfo.rangesrv_num > 0) {	
		//Get the master range server rank according the range server comm
		if ((ret = MPI_Comm_size(index->rs_comm, &master)) != MPI_SUCCESS) {
			mlog(MPI_CRIT, "Rank: %d - " 
			     "Error getting size of comm", 
			     md->mdhim_rank);			
		}		
		//The master rank is the last rank in range server comm
		master--;

		//First we send the number of items that we are going to send
		//Allocate the receive buffer size
		recvsize = index->num_rangesrvs * sizeof(int);
		recvbuf = malloc(recvsize);
		memset(recvbuf, 0, recvsize);
		MPI_Barrier(index->rs_comm);
		//The master server will receive the number of stats each server has
		if ((ret = MPI_Gather(&num_items, 1, MPI_UNSIGNED, recvbuf, 1,
				      MPI_INT, master, index->rs_comm)) != MPI_SUCCESS) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - " 
			     "Error while receiving the number of statistics from each range server", 
			     md->mdhim_rank);
			free(recvbuf);
			goto error;
		}
		
		num_items = 0;
		displs = malloc(sizeof(int) * index->num_rangesrvs);
		recvcounts = malloc(sizeof(int) * index->num_rangesrvs);
		for (i = 0; i < index->num_rangesrvs; i++) {
			displs[i] = num_items * stat_size;
			num_items += ((int *)recvbuf)[i];
			recvcounts[i] = ((int *)recvbuf)[i] * stat_size;
		}
		
		free(recvbuf);
		recvbuf = NULL;

		//Allocate send buffer
		sendbuf = malloc(sendsize);		  

		//Pack the stat data I have by iterating through the stats hash table
		ret =  pack_stats(index, sendbuf, sendsize,
				  float_type, stat_size, index->rs_comm);
		if (ret != MPI_SUCCESS) {
			free(recvbuf);
			goto error;
		}	

		//Allocate the recv buffer for the master range server
		if (md->mdhim_rank == index->rangesrv_master) {
			recvsize = num_items * stat_size;
			recvbuf = malloc(recvsize);
			memset(recvbuf, 0, recvsize);		
		} else {
			recvbuf = NULL;
			recvsize = 0;
		}

		MPI_Barrier(index->rs_comm);
		//The master server will receive the stat info from each rank in the range server comm
		if ((ret = MPI_Gatherv(sendbuf, sendsize, MPI_PACKED, recvbuf, recvcounts, displs,
				       MPI_PACKED, master, index->rs_comm)) != MPI_SUCCESS) {
			mlog(MDHIM_SERVER_CRIT, "Rank: %d - " 
			     "Error while receiving range server info", 
			     md->mdhim_rank);			
			goto error;
		}
		free(recvcounts);
		free(displs);
		free(sendbuf);	
	} 

	MPI_Barrier(md->mdhim_client_comm);
	//The master range server broadcasts the number of stats it is going to send
	if ((ret = MPI_Bcast(&num_items, 1, MPI_UNSIGNED, index->rangesrv_master,
			     md->mdhim_comm)) != MPI_SUCCESS) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - " 
		     "Error while receiving the number of stats to receive", 
		     md->mdhim_rank);
		goto error;
	}
	MPI_Barrier(md->mdhim_client_comm);

	recvsize = num_items * stat_size;
	//Allocate the receive buffer size for clients
	if (md->mdhim_rank != index->rangesrv_master) {
		recvbuf = malloc(recvsize);
		memset(recvbuf, 0, recvsize);
	}
	
	//The master range server broadcasts the receive buffer to the mdhim_comm
	if ((ret = MPI_Bcast(recvbuf, recvsize, MPI_PACKED, index->rangesrv_master,
			     md->mdhim_comm)) != MPI_SUCCESS) {
		mlog(MPI_CRIT, "Rank: %d - " 
		     "Error while receiving range server info", 
		     md->mdhim_rank);
		goto error;
	}

	//Unpack the receive buffer and populate our index->stats hash table
	recvidx = 0;
	for (i = 0; i < recvsize; i+=stat_size) {
		tstat = malloc(stat_size);
		memset(tstat, 0, stat_size);
		if ((ret = MPI_Unpack(recvbuf, recvsize, &recvidx, tstat, stat_size, 
				      MPI_CHAR, md->mdhim_comm)) != MPI_SUCCESS) {
			mlog(MPI_CRIT, "Rank: %d - " 
			     "Error while unpacking stat data", 
			     md->mdhim_rank);
			free(tstat);
			goto error;
		}	

		stat = malloc(sizeof(struct mdhim_stat));
		stat->dirty = 0;
		if (float_type) {
			stat->min = (void *) malloc(sizeof(long double));
			stat->max = (void *) malloc(sizeof(long double));
			if (index->key_type == MDHIM_BURSTFS_KEY) {
				struct mdhim_db_fstat * tmp_stat;
				tmp_stat = (struct mdhim_db_fstat *)tstat;
				memcpy((char *)stat->min, (char *)&(tmp_stat->dmin), sizeof(ulong));
				memcpy((char *)(stat->min)+sizeof(ulong), \
						(char *)&(tmp_stat->dmin)+sizeof(ulong), sizeof(ulong));
				memcpy((char *)stat->max, (char *)&(tmp_stat->dmax), sizeof(ulong));
				memcpy((char *)(stat->max)+sizeof(ulong), \
						(char *)&(tmp_stat->dmax)+sizeof(ulong), sizeof(ulong));
	/*			printf("abcd:min fid is %ld, min offset is %ld, \
					max fid is %ld, max offset is %ld, rank is %d\n", \
					*((ulong *)stat->min), *((ulong *)stat->min+1), \
					*((ulong *)stat->max), *((ulong *)stat->max+1), \
					md->mdhim_rank);
				fflush(stdout);
	 */
			}
			else {
				*(long double *)stat->min = ((struct mdhim_db_fstat *)tstat)->dmin;
				*(long double *)stat->max = ((struct mdhim_db_fstat *)tstat)->dmax;
			}
			stat->key = ((struct mdhim_db_fstat *)tstat)->slice;
	//		printf("abcd:slice is %ld\n", stat->key);
			stat->num = ((struct mdhim_db_fstat *)tstat)->num;
		} else {
			stat->min = (void *) malloc(sizeof(uint64_t));
			stat->max = (void *) malloc(sizeof(uint64_t));
			*(uint64_t *)stat->min = ((struct mdhim_db_istat *)tstat)->imin;
			*(uint64_t *)stat->max = ((struct mdhim_db_istat *)tstat)->imax;
			stat->key = ((struct mdhim_db_istat *)tstat)->slice;
			stat->num = ((struct mdhim_db_istat *)tstat)->num;
		}
		  
		HASH_FIND_INT(index->stats, &stat->key, tmp);
		if (!tmp) {
			HASH_ADD_INT(index->stats, key, stat); 
		} else {	
			//Replace the existing stat
			HASH_REPLACE_INT(index->stats, key, stat, tmp);  
			free(tmp);
		}
		free(tstat);
	}
	free(recvbuf);
	return MDHIM_SUCCESS;

error:
	if (recvbuf) {
		free(recvbuf);
	}

	return MDHIM_ERROR;
}

int get_stat_flush_local(struct mdhim_t *md, struct index_t *index) {
	char *sendbuf;
	int sendsize = 0;
	int recvidx = 0;
	char *recvbuf;
	int *num_items_to_recv;
	int *recvcounts;
	int *displs;
	int recvsize;
	int ret = 0;
	int i = 0, j;
	int float_type = 0;
	struct mdhim_stat *stat, *tmp, *rank_stat;
	void *tstat;
	int stat_size = 0;
	int num_items = 0;
	
	//Determine the size of the buffers to send based on the number and type of stats
	if ((ret = is_float_key(index->key_type)) == 1) {
		float_type = 1;
		stat_size = sizeof(struct mdhim_db_fstat);
	} else {
		float_type = 0;
		stat_size = sizeof(struct mdhim_db_istat);
	}

	recvbuf = NULL;
	if (index->myinfo.rangesrv_num > 0) {
		//Get the number stats in our hash table
		if (index->mdhim_store->mdhim_store_stats) {
			num_items = HASH_COUNT(index->mdhim_store->mdhim_store_stats);
		} else {
			num_items = 0;
		}
		if ((ret = is_float_key(index->key_type)) == 1) {
			sendsize = num_items * sizeof(struct mdhim_db_fstat);
		} else {
			sendsize = num_items * sizeof(struct mdhim_db_istat);
		}
	}

	//First we send the number of items that we are going to send
	//Allocate the receive buffer size
	recvsize = md->mdhim_comm_size * sizeof(int);
	recvbuf = malloc(recvsize);
	memset(recvbuf, 0, recvsize);
	MPI_Barrier(md->mdhim_client_comm);
	//All gather the number of items to send
	if ((ret = MPI_Allgather(&num_items, 1, MPI_UNSIGNED, recvbuf, 1,
				 MPI_INT, md->mdhim_comm)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - " 
		     "Error while receiving the number of statistics from each range server", 
		     md->mdhim_rank);
		free(recvbuf);
		goto error;
	}
		
	num_items = 0;
	displs = malloc(sizeof(int) * md->mdhim_comm_size);
	recvcounts = malloc(sizeof(int) * md->mdhim_comm_size);
	for (i = 0; i < md->mdhim_comm_size; i++) {
		displs[i] = num_items * stat_size;
		num_items += ((int *)recvbuf)[i];
		recvcounts[i] = ((int *)recvbuf)[i] * stat_size;
	}

	num_items_to_recv = (int *)recvbuf;
	recvbuf = NULL;

	if (sendsize) {
		//Allocate send buffer
		sendbuf = malloc(sendsize);		  
	
		//Pack the stat data I have by iterating through the stats hash table
		ret =  pack_stats(index, sendbuf, sendsize,
				  float_type, stat_size, md->mdhim_comm);
		if (ret != MPI_SUCCESS) {
			free(recvbuf);
			goto error;
		}	
	} else {
		sendbuf = NULL;
	}

	recvsize = num_items * stat_size;
	recvbuf = malloc(recvsize);
	memset(recvbuf, 0, recvsize);		

	MPI_Barrier(md->mdhim_client_comm);
	//The master server will receive the stat info from each rank in the range server comm
	if ((ret = MPI_Allgatherv(sendbuf, sendsize, MPI_PACKED, recvbuf, recvcounts, displs,
				   MPI_PACKED, md->mdhim_comm)) != MPI_SUCCESS) {
		mlog(MDHIM_SERVER_CRIT, "Rank: %d - " 
		     "Error while receiving range server info", 
		     md->mdhim_rank);			
		goto error;
	}

	free(recvcounts);
	free(displs);
	free(sendbuf);	


	MPI_Barrier(md->mdhim_client_comm);

	//Unpack the receive buffer and populate our index->stats hash table
	recvidx = 0;
	for (i = 0; i < md->mdhim_comm_size; i++) {
		if ((ret = is_range_server(md, i, index)) < 1) {
			continue;
		}

		HASH_FIND_INT(index->stats, &i, tmp);
		if (!tmp) {
			mlog(MPI_CRIT, "Rank: %d - " 
			     "Adding rank: %d to local index stat data", 
			     md->mdhim_rank, i);
			rank_stat = malloc(sizeof(struct mdhim_stat));
			memset(rank_stat, 0, sizeof(struct mdhim_stat));
			rank_stat->key = i;
			rank_stat->stats = NULL;
			HASH_ADD_INT(index->stats, key, rank_stat); 
		} else {		
			rank_stat = tmp;	       
		}

		for (j = 0; j < num_items_to_recv[i]; j++) {
			tstat = malloc(stat_size);
			memset(tstat, 0, stat_size);
			if ((ret = MPI_Unpack(recvbuf, recvsize, &recvidx, tstat, stat_size, 
					      MPI_CHAR, md->mdhim_comm)) != MPI_SUCCESS) {
				mlog(MPI_CRIT, "Rank: %d - " 
				     "Error while unpacking stat data", 
				     md->mdhim_rank);
				free(tstat);
				goto error;
			}	

			stat = malloc(sizeof(struct mdhim_stat));
			stat->dirty = 0;
			if (float_type) {
				stat->min = (void *) malloc(sizeof(long double));
				stat->max = (void *) malloc(sizeof(long double));
				*(long double *)stat->min = ((struct mdhim_db_fstat *)tstat)->dmin;
				*(long double *)stat->max = ((struct mdhim_db_fstat *)tstat)->dmax;
				stat->key = ((struct mdhim_db_fstat *)tstat)->slice;
				stat->num = ((struct mdhim_db_fstat *)tstat)->num;
			} else {
				stat->min = (void *) malloc(sizeof(uint64_t));
				stat->max = (void *) malloc(sizeof(uint64_t));
				*(uint64_t *)stat->min = ((struct mdhim_db_istat *)tstat)->imin;
				*(uint64_t *)stat->max = ((struct mdhim_db_istat *)tstat)->imax;
				stat->key = ((struct mdhim_db_istat *)tstat)->slice;
				stat->num = ((struct mdhim_db_istat *)tstat)->num;
			}
		  
			mlog(MPI_CRIT, "Rank: %d - " 
			     "Adding rank: %d with stat min: %lu, stat max: %lu, stat key: %u num: %lu" 
			     "to local index stat data", 
			     md->mdhim_rank, i, *(uint64_t *)stat->min, *(uint64_t *)stat->max, 
			     stat->key, stat->num);
			HASH_FIND_INT(rank_stat->stats, &stat->key, tmp);
			if (!tmp) {
				HASH_ADD_INT(rank_stat->stats, key, stat); 
			} else {	
				//Replace the existing stat
				HASH_REPLACE_INT(rank_stat->stats, key, stat, tmp);
				free(tmp);
			}

			free(tstat);
		}
	}

	free(recvbuf);
	free(num_items_to_recv);

	return MDHIM_SUCCESS;

error:
	if (recvbuf) {
		free(recvbuf);
	}

	return MDHIM_ERROR;
}

/**
 * get_stat_flush
 * Receives stat data from all the range servers and populates md->stats
 *
 * @param md      in   main MDHIM struct
 * @return MDHIM_SUCCESS or MDHIM_ERROR on error
 */
int get_stat_flush(struct mdhim_t *md, struct index_t *index) {
	int ret;

	pthread_mutex_lock(md->mdhim_comm_lock);

	if (index->type != LOCAL_INDEX) {
		ret = get_stat_flush_global(md, index);
	} else {
		ret = get_stat_flush_local(md, index);
	}

	pthread_mutex_unlock(md->mdhim_comm_lock);

	return ret;
}

