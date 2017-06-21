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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <linux/limits.h>
#include <sys/time.h>
#include "ds_leveldb.h"

struct timeval dbputstart, dbputend;
struct timeval dbgetstart, dbgetend;
double dbputtime=0, dbgettime=0;

struct timeval dbngetstart, dbngetend;
double dbngettime=0;

struct timeval dbbputstart, dbbputend;
double dbbputtime=0;

extern int dbg_rank;
static void cmp_destroy(void* arg) { }

static int cmp_empty(const char* a, size_t alen,
		     const char* b, size_t blen) {
	int ret = 2;
	if (a && !b) {
		return 1;
	} else if (!a && b) {
		return -1;
	} else if (!a && !b) {
		return 0;
	}

	if (alen > blen) {
		return 1;
	} else if (blen > alen) {
		return -1;
	} 

	return ret;
}

int cmp_int_compare(void* arg, const char* a, size_t alen,
			   const char* b, size_t blen) {
	int ret;

	ret = cmp_empty(a, alen, b, blen);
	if (ret != 2) {
		return ret;
	}
	if (*(uint32_t *) a < *(uint32_t *) b) {
		ret = -1;
	} else if (*(uint32_t *) a == *(uint32_t *) b) {
		ret = 0;
	} else {
		ret = 1;
	}

	return ret;
}

int cmp_lint_compare(void* arg, const char* a, size_t alen,
			   const char* b, size_t blen) {
	int ret;

	ret = cmp_empty(a, alen, b, blen);
	if (ret != 2) {
		return ret;
	}
	if (*(uint64_t *) a < *(uint64_t *) b) {
		ret = -1;
	} else if (*(uint64_t *) a == *(uint64_t *) b) {
		ret = 0;
	} else {
		ret = 1;
	}

	return ret;
}

static int cmp_double_compare(void* arg, const char* a, size_t alen,
			      const char* b, size_t blen) {
	int ret;

	ret = cmp_empty(a, alen, b, blen);
	if (ret != 2) {
		return ret;
	}
	if (*(double *) a < *(double *) b) {
		ret = -1;
	} else if (*(double *) a == *(double *) b) {
		ret = 0;
	} else {
		ret = 1;
	}

	return ret;
}

static int cmp_float_compare(void* arg, const char* a, size_t alen,
			   const char* b, size_t blen) {
	int ret;

	ret = cmp_empty(a, alen, b, blen);
	if (ret != 2) {
		return ret;
	}
	if (*(float *) a < *(float *) b) {
		ret = -1;
	} else if (*(float *) a == *(float *) b) {
		ret = 0;
	} else {
		ret = 1;
	}

	return ret;
}


// For string, first compare for null pointers, then for order
// up to a null character or the given lengths.
static int cmp_string_compare(void* arg, const char* a, size_t alen,
			   const char* b, size_t blen) {
    int idx;

    if (a && !b) {
            return 1;
    } else if (!a && b) {
            return -1;
    } else if (!a && !b) {
            return 0;
    }

    // Do this wile they are equal and we have not reached the end of one of them
    for(idx=0; *a == *b && *a != '\0' && *b != '\0' && idx<alen && idx<blen; ) {
        idx++;
        a++;
        b++;
    }

    // If we are at the end and no difference is found, then they are equal
    if( (*a == '\0' && *b == '\0') || (alen == blen && idx == alen)) {
       return 0;
    } else if ((alen == idx || *a == '\0') && alen < blen) { // end of a?
        return -1;
    } else if ((blen == idx || *b == '\0') && blen < alen) { // end of b?
        return 1;
    } else if ( *a > *b ) { // else compare the two different characters to decide
       return 1;
    }

    // If none of the above, then b is greater
    return -1;
}

static int cmp_byte_compare(void* arg, const char* a, size_t alen,
			    const char* b, size_t blen) {
	int ret;

	long offset, old_offset;
	long fid, old_fid;

	fid = *((unsigned long *)a);
	old_fid = *((unsigned long *)b);

	offset = *((unsigned long *)a+1);
	old_offset = *((unsigned long *)b+1);

	ret = fid - old_fid;

	if (ret != 0)
			return ret;
	else {
		if (offset - old_offset > 0)
				return 1;
		else if(offset -old_offset < 0)
				return -1;
		else
				return 0;	
	}	

//	ret = memcmp(a, b, alen);
	return ret;
}

static int cmp_burstfs_compare(void* arg, const char* a, size_t alen,
			    const char* b, size_t blen) {
	int ret;

	long offset, old_offset;
	long fid, old_fid;

	fid = *((unsigned long *)a);
	old_fid = *((unsigned long *)b);

	offset = *((unsigned long *)a+1);
	old_offset = *((unsigned long *)b+1);

	ret = fid - old_fid;

	if (ret != 0)
			return ret;
	else {
		if (offset - old_offset > 0)
				return 1;
		else if(offset - old_offset < 0)
				return -1;
		else
				return 0;
	}

//	ret = memcmp(a, b, alen);
	return ret;
}

static const char* cmp_name(void* arg) {
	return "mdhim_cmp";
}

/**
 * mdhim_leveldb_open
 * Opens the database
 *
 * @param dbh            in   double pointer to the leveldb handle
 * @param dbs            in   double pointer to the leveldb statistics db handle 
 * @param path           in   path to the database file
 * @param flags          in   flags for opening the data store
 * @param mstore_opts    in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */

int mdhim_leveldb_open(void **dbh, void **dbs, char *path, int flags, int key_type, struct mdhim_options_t *opts) {
	struct mdhim_leveldb_t *mdhimdb;
	struct mdhim_leveldb_t *statsdb;
	leveldb_t *db;
	char *err = NULL;
	char stats_path[PATH_MAX];

	mdhimdb = malloc(sizeof(struct mdhim_leveldb_t));
	memset(mdhimdb, 0, sizeof(struct mdhim_leveldb_t));
	statsdb = malloc(sizeof(struct mdhim_leveldb_t));
	memset(statsdb, 0, sizeof(struct mdhim_leveldb_t));

	//Create the options for the main database
	mdhimdb->options = leveldb_options_create();
	leveldb_options_set_create_if_missing(mdhimdb->options, 1);
	//leveldb_options_set_compression(options, 0);
	mdhimdb->filter = leveldb_filterpolicy_create_bloom(256);
	mdhimdb->cache = leveldb_cache_create_lru(8388608);
	mdhimdb->env = leveldb_create_default_env();
	mdhimdb->write_options = leveldb_writeoptions_create();
	leveldb_writeoptions_set_sync(mdhimdb->write_options, 0);
	mdhimdb->read_options = leveldb_readoptions_create();
	leveldb_options_set_cache(mdhimdb->options, mdhimdb->cache);
	leveldb_options_set_filter_policy(mdhimdb->options, mdhimdb->filter);
	//leveldb_options_set_max_open_files(mdhimdb->options, 10000);
	leveldb_options_set_max_open_files(mdhimdb->options, 10000);
	leveldb_options_set_write_buffer_size(mdhimdb->options, 1048576);
	leveldb_options_set_env(mdhimdb->options, mdhimdb->env);
	//Create the options for the stat database
	statsdb->options = leveldb_options_create();
	leveldb_options_set_create_if_missing(statsdb->options, 1);
	//leveldb_options_set_compression(stat_options, 0);
	statsdb->filter = leveldb_filterpolicy_create_bloom(16);       
	statsdb->cache = leveldb_cache_create_lru(1024);
	statsdb->env = leveldb_create_default_env();
	statsdb->write_options = leveldb_writeoptions_create();
	leveldb_writeoptions_set_sync(statsdb->write_options, 0);
	statsdb->read_options = leveldb_readoptions_create();
	leveldb_options_set_cache(statsdb->options, statsdb->cache);
	leveldb_options_set_filter_policy(statsdb->options, statsdb->filter);
	leveldb_options_set_write_buffer_size(statsdb->options, 1024);
	leveldb_options_set_env(statsdb->options, statsdb->env);

	switch(key_type) {
	case MDHIM_INT_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_int_compare, cmp_name);
		mdhimdb->compare = cmp_int_compare;
		break;
	case MDHIM_LONG_INT_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_lint_compare, cmp_name);
		mdhimdb->compare = cmp_lint_compare;
		break;
	case MDHIM_FLOAT_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_float_compare, cmp_name);
		mdhimdb->compare = cmp_float_compare;
		break;
	case MDHIM_DOUBLE_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_double_compare, cmp_name);
		mdhimdb->compare = cmp_double_compare;
		break;
	case MDHIM_STRING_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_string_compare, cmp_name);
		mdhimdb->compare = cmp_string_compare;
		break;
	case MDHIM_BURSTFS_KEY:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_burstfs_compare, cmp_name);
		mdhimdb->compare = cmp_burstfs_compare;
	default:
		mdhimdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_byte_compare, cmp_name);
		mdhimdb->compare = cmp_byte_compare;
		break;
	}
	
	leveldb_options_set_comparator(mdhimdb->options, mdhimdb->cmp);
	//Check to see if the given path + "_stat" and the null char will be more than the max
	if (strlen(path) + 6 > PATH_MAX) {
		mlog(MDHIM_SERVER_CRIT, "Error opening leveldb database - path provided is too long");
		return MDHIM_DB_ERROR;
	}

	//Open the main database
	db = leveldb_open(mdhimdb->options, path, &err);

	fflush(stdout);
	mdhimdb->db = db;
	//Set the output handle
	*((struct mdhim_leveldb_t **) dbh) = mdhimdb;
	if (err != NULL) {
		mlog(MDHIM_SERVER_CRIT, "Error opening leveldb database, abc..., path is %s", path);
		return MDHIM_DB_ERROR;
	}

	//Open the stats database
	sprintf(stats_path, "%s_stats", path);
	statsdb->compare = cmp_int_compare;
	statsdb->cmp = leveldb_comparator_create(NULL, cmp_destroy, cmp_int_compare, cmp_name);
	leveldb_options_set_comparator(statsdb->options, statsdb->cmp);
	db = leveldb_open(statsdb->options, stats_path, &err);

	statsdb->db = db;
	*((struct mdhim_leveldb_t **) dbs) = statsdb;

	if (err != NULL) {
		mlog(MDHIM_SERVER_CRIT, "Error opening leveldb database, def..., stats_path is %s", stats_path);
		return MDHIM_DB_ERROR;
	}

	return MDHIM_SUCCESS;
}

/**
 * mdhim_leveldb_put
 * Stores a single key in the data store
 *
 * @param dbh         in   pointer to the leveldb handle
 * @param key         in   void * to the key to store
 * @param key_len     in   length of the key
 * @param data        in   void * to the value of the key
 * @param data_len    in   length of the value data 
 * @param mstore_opts in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_put(void *dbh, void *key, int key_len, void *data, int32_t data_len) {
    leveldb_writeoptions_t *options;
    char *err = NULL;
    struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
    struct timeval start, end;
    
    gettimeofday(&start, NULL);
    options = mdhimdb->write_options;    	    
    leveldb_put(mdhimdb->db, options, key, key_len, data, data_len, &err);
    /*
     * temporarily mute the error message until the file metadata
     * operation is fully defined and implemented */

    if (err != NULL) {
     /*   printf("in mdhim, rank:%d, key is %d, value len is %d, err is %s\n",\
        		dbg_rank, *((int *)key), data_len, err);
        fflush(stdout); */
	    mlog(MDHIM_SERVER_CRIT, "Error putting key/value in leveldb");
	    return MDHIM_DB_ERROR;
    }

    mlog(MDHIM_SERVER_DBG, "Took: %d seconds to put the record", 
	 (int) (end.tv_sec - start.tv_sec));

    return MDHIM_SUCCESS;
}

/**
 * mdhim_leveldb_batch_put
 * Stores multiple keys in the data store
 *
 * @param dbh          in   pointer to the leveldb handle
 * @param keys         in   void ** to the key to store
 * @param key_lens     in   int * to the lengths of the keys
 * @param data         in   void ** to the values of the keys
 * @param data_lens    in   int * to the lengths of the value data 
 * @param num_records  in   int for the number of records to insert 
 * @param mstore_opts  in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_batch_put(void *dbh, void **keys, int32_t *key_lens, 
			    void **data, int32_t *data_lens, int num_records) {
	gettimeofday(&dbbputstart, NULL);
	leveldb_writeoptions_t *options;
	char *err = NULL;
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	struct timeval start, end;
	leveldb_writebatch_t* write_batch;
	int i;

	gettimeofday(&start, NULL);
	write_batch = leveldb_writebatch_create();
	options = mdhimdb->write_options;   
	for (i = 0; i < num_records; i++) {
/*			printf("in ds, fid is %d, offset is %ld, nodeid is %ld, len %ld, key_len:%d, data_lens:%d, num_records:%ld\n", *((long *)(keys[i])),\
			 *((long *)keys[i]+1), *(((long *)(data[i]))),\
				 *((long *)((data[i]))+1), key_lens[i], data_lens[i], num_records);
			fflush(stdout);
*/
		leveldb_writebatch_put(write_batch, keys[i], key_lens[i], 
				       data[i], data_lens[i]);
	}

	leveldb_write(mdhimdb->db, options, write_batch, &err);
	leveldb_writebatch_destroy(write_batch);
	if (err != NULL) {
		mlog(MDHIM_SERVER_CRIT, "Error in batch put in leveldb");
		return MDHIM_DB_ERROR;
	}
	
	gettimeofday(&end, NULL);
    gettimeofday(&end, NULL);

    gettimeofday(&dbbputend, NULL);
	dbbputtime+=1000000*(dbbputend.tv_sec-dbbputstart.tv_sec)+dbbputend.tv_usec-dbbputstart.tv_usec;
	mlog(MDHIM_SERVER_DBG, "Took: %d seconds to put %d records", 
	     (int) (end.tv_sec - start.tv_sec), num_records);
	
	return MDHIM_SUCCESS;
}

/**
 * mdhim_leveldb_get
 * Gets a value, given a key, from the data store
 *
 * @param dbh          in   pointer to the leveldb db handle
 * @param key          in   void * to the key to retrieve the value of
 * @param key_len      in   length of the key
 * @param data         out  void * to the value of the key
 * @param data_len     out  pointer to length of the value data 
 * @param mstore_opts  in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_get(void *dbh, void *key, int key_len, void **data, int32_t *data_len) {
/*
	printf("in getting\n");
	fflush(stdout);
*/
	leveldb_readoptions_t *options;
	char *err = NULL;
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	int ret = MDHIM_SUCCESS;
	void *ldb_data;
	size_t ldb_data_len = 0;

	options = mdhimdb->read_options;
	*data = NULL;
	gettimeofday(&dbgetstart, NULL);
	ldb_data = leveldb_get(mdhimdb->db, options, key, key_len, &ldb_data_len, &err);
	if (err != NULL) {
		mlog(MDHIM_SERVER_CRIT, "Error getting value in leveldb");
		return MDHIM_DB_ERROR;
	}

	if (!ldb_data_len) {
		ret = MDHIM_DB_ERROR;
		return ret;
	}

	*data_len = ldb_data_len;
	*data = malloc(*data_len);
	memcpy(*data, ldb_data, *data_len);
	free(ldb_data);
	gettimeofday(&dbgetend, NULL);
	dbgettime+=1000000*(dbgetend.tv_sec-dbgetstart.tv_sec) + \
		dbgetend.tv_usec-dbgetstart.tv_usec;
	return ret;
}

/**
 * mdhim_leveldb_get_next
 * Gets the next key/value from the data store
 *
 * @param dbh             in   pointer to the unqlite db handle
 * @param key             out  void ** to the key that we get
 * @param key_len         out  int * to the length of the key 
 * @param data            out  void ** to the value belonging to the key
 * @param data_len        out  int * to the length of the value data 
 * @param mstore_opts in   additional cursor options for the data store layer 
 * 
 */
int mdhim_leveldb_get_next(void *dbh, void **key, int *key_len, 
			   void **data, int32_t *data_len) {
	leveldb_readoptions_t *options;
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	int ret = MDHIM_SUCCESS;
	leveldb_iterator_t *iter;
	const char *res;
	int len = 0;
	void *old_key;
	int old_key_len;
	struct timeval start, end;
	int cmp_ret = -5;

	gettimeofday(&dbngetstart, NULL);
	//Init the data to return
	*data = NULL;
	*data_len = 0;

	gettimeofday(&start, NULL);
	//Create the options and iterator
	options = mdhimdb->read_options;
	old_key = *key;
	old_key_len = *key_len;
	*key = NULL;
	*key_len = 0;


	iter = leveldb_create_iterator(mdhimdb->db, options);

	//If the user didn't supply a key, then seek to the first
	if (!old_key || old_key_len == 0) {

		leveldb_iter_seek_to_first(iter);
	} else {

		/* Seek to the passed in key.  If that doesn't exist, iterate until we find one greater
		   or until we exhaust the keys.*/
		leveldb_iter_seek(iter, old_key, old_key_len);

		if (!leveldb_iter_valid(iter)) {

			leveldb_iter_seek_to_first(iter);

			while(leveldb_iter_valid(iter)) {

				res = leveldb_iter_key(iter, (size_t *) &len);

				if ((cmp_ret = mdhimdb->compare(NULL, res, len,\
						old_key, old_key_len)) > 0) {
					break;
				}
				
				leveldb_iter_next(iter);
			}			
		} else {

			if (mdhimdb->compare(NULL, (leveldb_iter_key(iter,\
					(size_t *) &len)), len, old_key, old_key_len) == 0)
				leveldb_iter_next(iter);
		}
	}

	if (!leveldb_iter_valid(iter)) {
		/*
		printf("error 1\n");
		fflush(stdout);
		*/
		goto error;
	}

	res = leveldb_iter_value(iter, (size_t *) &len);
	if (res) {
		*data = malloc(len);
		memcpy(*data, res, len);
		*data_len = len;
	} else {
		*data = NULL;
		*data_len = 0;
	}

	res = leveldb_iter_key(iter, (size_t *) key_len);
	if (res) {
		*key = malloc(*key_len);
		memcpy(*key, res, *key_len);
	} else {
		*key = NULL;
		*key_len = 0;
	}

	if (!*data) {
		goto error;
		/*
		printf("error 2\n");
		fflush(stdout);
		*/
	}

        //Destroy iterator
	leveldb_iter_destroy(iter);
	gettimeofday(&end, NULL);
	mlog(MDHIM_SERVER_DBG, "Took: %d seconds to get the next record", 
	     (int) (end.tv_sec - start.tv_sec));
	gettimeofday(&dbngetend, NULL);
	dbngettime += 1000000*(dbngetend.tv_sec-dbngetstart.tv_sec)+dbngetend.tv_usec-dbngetstart.tv_usec;
	return ret;

error:	
	gettimeofday(&dbngetend, NULL);
	dbngettime += 1000000*(dbngetend.tv_sec-dbngetstart.tv_sec)+dbngetend.tv_usec-dbngetstart.tv_usec; 
	 //Destroy iterator
	leveldb_iter_destroy(iter);      
	*key = NULL;
	*key_len = 0;
	*data = NULL;
	*data_len = 0;
	return MDHIM_DB_ERROR;
}


/**
 * mdhim_leveldb_get_prev
 * Gets the prev key/value from the data store
 *
 * @param dbh             in   pointer to the unqlite db handle
 * @param key             out  void ** to the key that we get
 * @param key_len         out  int * to the length of the key 
 * @param data            out  void ** to the value belonging to the key
 * @param data_len        out  int * to the length of the value data 
 * @param mstore_opts in   additional cursor options for the data store layer 
 * 
 */
int mdhim_leveldb_get_prev(void *dbh, void **key, int *key_len, 
			   void **data, int32_t *data_len) {
	leveldb_readoptions_t *options;
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	int ret = MDHIM_SUCCESS;
	leveldb_iterator_t *iter;
	const char *res;
	int len = 0;
	void *old_key;
	int old_key_len;
	struct timeval start, end;

	//Init the data to return
	*data = NULL;
	*data_len = 0;

	gettimeofday(&start, NULL);

	//Create the options and iterator
	options = mdhimdb->read_options;
	old_key = *key;
	old_key_len = *key_len;
	*key = NULL;
	*key_len = 0;

	iter = leveldb_create_iterator(mdhimdb->db, options);

	//If the user didn't supply a key, then seek to the first
	if (!old_key || old_key_len == 0) {
		leveldb_iter_seek_to_last(iter);
	} else {
		leveldb_iter_seek(iter, old_key, old_key_len);
		if (!leveldb_iter_valid(iter)) { 
			leveldb_iter_seek_to_last(iter);
			while(leveldb_iter_valid(iter)) {
				res = leveldb_iter_key(iter, (size_t *) &len);
				if (mdhimdb->compare(NULL, res, len, old_key, old_key_len) < 0) {
					break;
				}
				
				leveldb_iter_prev(iter);
			}			
		} else {
			leveldb_iter_prev(iter);
		}
	}

	if (!leveldb_iter_valid(iter)) {
		goto error;
	}

	res = leveldb_iter_value(iter, (size_t *) &len);
	if (res) {
		*data = malloc(len);
		memcpy(*data, res, len);
		*data_len = len;
	} else {
		*data = NULL;
		*data_len = 0;
	}

	res = leveldb_iter_key(iter, (size_t *) key_len);
	if (res) {
		*key = malloc(*key_len);
		memcpy(*key, res, *key_len);
	} else {
		*key = NULL;
		*key_len = 0;
	}

	if (!*data) {
		goto error;
	}

        //Destroy iterator
	leveldb_iter_destroy(iter);      
	gettimeofday(&end, NULL);
	mlog(MDHIM_SERVER_DBG, "Took: %d seconds to get the previous record", 
	     (int) (end.tv_sec - start.tv_sec));
	return ret;

error:	
	 //Destroy iterator
	leveldb_iter_destroy(iter);      
	*key = NULL;
	*key_len = 0;
	*data = NULL;
	*data_len = 0;
	return MDHIM_DB_ERROR;
}

/**
 * mdhim_leveldb_close
 * Closes the data store
 *
 * @param dbh         in   pointer to the leveldb db handle 
 * @param dbs         in   pointer to the leveldb statistics db handle 
 * @param mstore_opts in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_close(void *dbh, void *dbs) {
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	struct mdhim_leveldb_t *statsdb = (struct mdhim_leveldb_t *) dbs;

	//Close the databases
	leveldb_close(mdhimdb->db);
	leveldb_close(statsdb->db);

	//Destroy the options
	leveldb_comparator_destroy(mdhimdb->cmp);
	leveldb_options_destroy(mdhimdb->options);
	leveldb_readoptions_destroy(mdhimdb->read_options);
	leveldb_writeoptions_destroy(mdhimdb->write_options);
	leveldb_filterpolicy_destroy(mdhimdb->filter);
	leveldb_comparator_destroy(statsdb->cmp);
	leveldb_options_destroy(statsdb->options);
	leveldb_readoptions_destroy(statsdb->read_options);
	leveldb_writeoptions_destroy(statsdb->write_options);
	leveldb_filterpolicy_destroy(statsdb->filter);

	free(mdhimdb);
	free(statsdb);

	return MDHIM_SUCCESS;
}

/**
 * mdhim_leveldb_del
 * delete the given key
 *
 * @param dbh         in   pointer to the leveldb db handle
 * @param key         in   void * for the key to delete
 * @param key_len     in   int for the length of the key
 * @param mstore_opts in   additional options for the data store layer 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_del(void *dbh, void *key, int key_len) {
	leveldb_writeoptions_t *options;
	char *err = NULL;
	struct mdhim_leveldb_t *mdhimdb = (struct mdhim_leveldb_t *) dbh;
	
	options = mdhimdb->write_options;
	leveldb_delete(mdhimdb->db, options, key, key_len, &err);
	if (err != NULL) {
		mlog(MDHIM_SERVER_CRIT, "Error deleting key in leveldb");
		return MDHIM_DB_ERROR;
	}
 
	return MDHIM_SUCCESS;
}

/**
 * mdhim_leveldb_commit
 * Commits outstanding writes the data store
 *
 * @param dbh         in   pointer to the leveldb handle 
 * 
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 */
int mdhim_leveldb_commit(void *dbh) {
	return MDHIM_SUCCESS;
}


/**
 * mdhim_levedb_batch_next
 * get next (tot_records) starting from key (inclusive)
 *
 * @param dbh         in   pointer to the leveldb db handle
 * @param key         in   a list of keys to be returned
 * @param key_len 	  in   a list of key_length to be returned
 * @param data		  in   a list values to be returned corresponding to the keys
 * @param data_len	  in   a list of value length to be returned
 * @param num_records in   actual number of key-value pairs returned
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 * @return
 */
int mdhim_levedb_batch_next(void *dbh, char **key, int *key_len, char **data, int32_t *data_len, \
		int tot_records, int *num_records) {

	gettimeofday(&dbngetstart, NULL);
	struct mdhim_leveldb_t *mdhim_db = (struct mdhim_leveldb_t *) dbh;
	int cursor = 0;
	leveldb_readoptions_t *options;
	leveldb_iterator_t *iter;
	const char *res;
	int len = 0;
	void *old_key;
	int old_key_len;

	options = mdhim_db->read_options;
	old_key = key[0];
	old_key_len = key_len[0];

	iter = leveldb_create_iterator(mdhim_db->db, options);
	if (!old_key || old_key_len == 0) {
		leveldb_iter_seek_to_first(iter);

	} else {
		leveldb_iter_seek(iter, (char *)old_key, old_key_len);

		while(leveldb_iter_valid(iter) && cursor != tot_records) {

			res = leveldb_iter_value(iter, (size_t *)&len);
			if (res) {
				data[cursor] = (char *)malloc(len);
				memcpy(data[cursor], res, len);
				data_len[cursor] = len;

			} else {
				data[cursor] = NULL;
				data_len[cursor] = 0;
				goto error;
			}

			res = leveldb_iter_key(iter, (size_t *)&len);
			if (res) {
				key[cursor] = (char *)malloc(len);
				memcpy(key[cursor], res, len);
				key_len[cursor] = len;


			} else {
				key[cursor] = NULL;
				key_len[cursor] = 0;
				goto error;
			}

			leveldb_iter_next(iter);
			(*num_records)++;
			cursor++;
		}
	}
	gettimeofday(&dbngetend, NULL);
	dbngettime +=\
			1000000 * (dbngetend.tv_sec - dbngetstart.tv_sec)\
			+ dbngetend.tv_usec - dbngetstart.tv_usec;
	leveldb_iter_destroy(iter);
	if (*num_records < tot_records)
		return MDHIM_DB_ERROR;
	else
		return 0;
error:
	gettimeofday(&dbngetend, NULL);
	dbngettime += 1000000 * (dbngetend.tv_sec - dbngetstart.tv_sec)\
			+ dbngetend.tv_usec - dbngetstart.tv_usec;
		 //Destroy iterator
	leveldb_iter_destroy(iter);
	return MDHIM_DB_ERROR;

}

/**
 * levedb_batch_ranges
 * get a list of key-value pairs that fall in the range of a list of
 * items identified (start_key, end_key)
 *
 * @param dbh         in   pointer to the leveldb db handle
 * @param key         in   a list of start_keys and end_keys.
 * odd indexed is start_key, even indexed key is end_key
 * @param key_len 	  in   a list of key_length for start_keys and end_keys
 * @param out_key	  in   a list keys to be returned corresponding to the start_keys/end_keys
 * @param out_key_len in   a list of key lengths to be returned
 * @param out_val     in   a list of values to be returned
 * @param out_val_len in   a list of value lens to be returned
 * @param tot_records in   number of start_keys and end_keys
 * @param out_records_cnt in number of copied key-value pairs
 * @param out_records_cap in number of allocated key-value pairs
 * @return MDHIM_SUCCESS on success or MDHIM_DB_ERROR on failure
 * @return
 */
int levedb_batch_ranges(void *dbh, char **key, int *key_len,\
		char ***out_key, int **out_key_len,\
			char ***out_val, int **out_val_len,\
				int tot_records, int *out_records_cnt) {

	int i;
	struct mdhim_leveldb_t *mdhim_db = (struct mdhim_leveldb_t *) dbh;

	int tmp_records_cnt = 0; /*the temporary number of out records*/
	int tmp_out_cap = tot_records/2; /* the temporary out capacity*/

	leveldb_iterator_t *iter;
	leveldb_readoptions_t *options;
	options = mdhim_db->read_options;

	iter = leveldb_create_iterator(mdhim_db->db, options);

	*out_val = (char **)malloc(tot_records/2 * sizeof(char *));
	*out_val_len = (int *)malloc(tot_records/2 * sizeof(int));
	*out_key = (char **)malloc(tot_records/2 * sizeof(char *));
	*out_key_len = (int *)malloc(tot_records/2 * sizeof(int));

	/*ToDo: return different error types if leveldb_process_range fails*/

	for (i = 0; i < tot_records/2; i++) {
/*		printf("%dth offset is %ld, fid is %d, %d offset is %ld, len:%ld\n", 2 * i, \
				BURSTFS_OFFSET(key[2 * i]), BURSTFS_FID(key[2 * i]), 2 * i + 1, \
					BURSTFS_OFFSET(key[ 2 * i + 1]), key_len[2 * i]); */
		leveldb_process_range(iter, key[2 * i], key[2 * i + 1], \
				key_len[2 * i], out_key, out_key_len, \
					out_val, out_val_len, &tmp_records_cnt, \
						&tmp_out_cap);

	}

	*out_records_cnt = tmp_records_cnt;

/*	printf("out_records_cnt is %d\n", *out_records_cnt);
	for (i = 0; i < *out_records_cnt; i++) {
		printf("%dth out offset is %ld, fid is %ld, addr is %ld\n", \
				i, BURSTFS_OFFSET((*out_key)[i]), BURSTFS_FID((*out_key)[i]), \
					BURSTFS_ADDR((*out_val)[i]));
		fflush(stdout);
	}
*/

	leveldb_iter_destroy(iter);
	return 0;
}

/*
 * for comments inside:
 * start: start_key
 * end: end_key
 * pre_start: the start of the key-value pair right before start_key
 * pre_end: the end of the key-value pair right before start_key
 * pre_end = pre_start + range of the key-value pair (length) - 1
 * start_f: the start of the key-value pair right after start_key
 * start_e: the end of the key-value pair right after start_key
 * start_e = start_f + range of the key-value pair (length) - 1
 * */
int leveldb_process_range(leveldb_iterator_t *iter,\
		char *start_key, char *end_key, \
			int key_len, char ***out_key, int **out_key_len, \
				char ***out_val, int **out_val_len, int *tmp_records_cnt, \
					int *tmp_out_cap) {

	const char *ret_key, *ret_val;
	long tmp_key_len, tmp_val_len;
	const char *save_next_ret_key, *save_next_ret_val;

	leveldb_iter_seek(iter, (char *)start_key, key_len);

	int diff_fid_flag = 0, data_end_flag = 0;
	if (!leveldb_iter_valid(iter)) {
		leveldb_iter_seek_to_last(iter);
		if (!leveldb_iter_valid(iter))
			return 0;

		ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
		if (!ret_key)
			return MDHIM_DB_ERROR;

		ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);
		if (!ret_val)
			return MDHIM_DB_ERROR;

		if (BURSTFS_FID(ret_key) != BURSTFS_FID(start_key))
			return 0;

		data_end_flag = 1;
	} else {
		ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
		if (!ret_key)
			return MDHIM_DB_ERROR;

		ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);
		if (!ret_val)
			return MDHIM_DB_ERROR;

	}

	if (BURSTFS_FID(start_key) != BURSTFS_FID(ret_key)) {
			leveldb_iter_prev(iter);
			if (!leveldb_iter_valid(iter)) {
				return 0;
			}

			ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
			if (!ret_key)
				return MDHIM_DB_ERROR;

			ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);
			if (!ret_val)
				return MDHIM_DB_ERROR;

			if (BURSTFS_FID(start_key) != BURSTFS_FID(ret_key))
				return 0;

			diff_fid_flag = 1;
	}

	if (data_end_flag || diff_fid_flag) {
		if (BURSTFS_OFFSET(start_key) > BURSTFS_OFFSET(ret_key)\
				+ BURSTFS_LEN(ret_val) - 1) {
			/*	pre_start,...........,pre_end; (start_f)..............(start_e)
			 		 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 	 start  end 	 */
			return 0;
		} else {

			long tmp_end;
			if (BURSTFS_OFFSET(end_key) >\
					BURSTFS_OFFSET(ret_key) + BURSTFS_LEN(ret_val) - 1) {
				/*	pre_start,...........,pre_end; (start_f)..............(start_e)


				 	 	 	 	 	 	 	 	 	 	 	 	start 	 	 	 	 	end 	 */
				tmp_end = BURSTFS_OFFSET(ret_key)\
						+ BURSTFS_LEN(ret_val) - 1;
			} else {
				/*	pre_start,...........,pre_end; (start_f)..............(start_e)


				 	 	 	 	 	 	 	 	 	 	 	 	start end 	 */
				tmp_end = BURSTFS_OFFSET(end_key);
			}

			char *ret_out_key = malloc(tmp_key_len);
			char *ret_out_val = malloc(tmp_val_len);
			memcpy(ret_out_key, ret_key, tmp_key_len);
			memcpy(ret_out_val, ret_val, tmp_val_len);

			BURSTFS_ADDR(ret_out_val) = BURSTFS_ADDR(ret_val)\
					+ BURSTFS_OFFSET(start_key) - BURSTFS_OFFSET(ret_key);
			BURSTFS_LEN(ret_out_val) = tmp_end - BURSTFS_OFFSET(start_key) + 1;
			BURSTFS_OFFSET(ret_out_key) = BURSTFS_OFFSET(start_key);

			add_kv(out_key, out_key_len, out_val,\
				out_val_len, tmp_records_cnt, tmp_out_cap, \
					ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);

			return 0;
		}

	} else {
		if (BURSTFS_OFFSET(ret_key) == BURSTFS_OFFSET(start_key)) {
			return	handle_next_half(iter,\
					start_key, end_key, \
						 out_key, out_key_len, \
							out_val, out_val_len, tmp_records_cnt, \
								tmp_out_cap);
		}

		leveldb_iter_prev(iter);
		if (!leveldb_iter_valid(iter)) {
			/*already the first, handle the next*/
			//start_next_half
			leveldb_iter_seek_to_first(iter);
			return handle_next_half(iter,\
					start_key, end_key, \
						out_key, out_key_len, \
							out_val, out_val_len, tmp_records_cnt, \
								tmp_out_cap);
		} else {
		    save_next_ret_key = ret_key;
		    save_next_ret_val = ret_val;


			ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
			if (!ret_key)
				return MDHIM_DB_ERROR;

			ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);
			if (!ret_val)
				return MDHIM_DB_ERROR;

			if (BURSTFS_FID(ret_key) != BURSTFS_FID(start_key)) {
				leveldb_iter_next(iter);
				return handle_next_half(iter,\
						start_key, end_key, \
							out_key, out_key_len, \
								out_val, out_val_len, tmp_records_cnt, \
									tmp_out_cap);

			}

			if (BURSTFS_OFFSET(start_key) <=\
					BURSTFS_OFFSET(ret_key) + BURSTFS_LEN(ret_val) - 1) {

				/*	pre_start,...........,pre_end; (start_f)..............(start_e)
				 	 	 	 	 start 		 end
				 	 	 	 	  	 	 	 	 	 	 	 	 	 		 */

				/*	pre_start,...........,pre_end; (start_f)..............(start_e)
				 	 	 	 	 start 							 end
				 	 	 	 	  	 	 	 	 	 	 	 	 	 		 */

				/*	pre_start,...........,pre_end; (start_f)..............(start_e)
				 	 	 	 	 start 							 							end
				 	  	 	 	 	 	 	 	 	 	 		 */
				int to_ret = 0;
				long tmp_end;
				if (BURSTFS_OFFSET(end_key) <=\
						BURSTFS_OFFSET(ret_key) + BURSTFS_LEN(ret_val) - 1) {
					to_ret = 1;
					tmp_end = BURSTFS_OFFSET(end_key);
				} else {
					tmp_end = BURSTFS_OFFSET(ret_key)\
							+ BURSTFS_LEN(ret_val) - 1;
				}

				char *ret_out_key = malloc(tmp_key_len);
				char *ret_out_val = malloc(tmp_val_len);
				memcpy(ret_out_key, ret_key, tmp_key_len);
				memcpy(ret_out_val, ret_val, tmp_val_len);

				BURSTFS_LEN(ret_out_val) = tmp_end - BURSTFS_OFFSET(start_key) + 1;
				BURSTFS_ADDR(ret_out_val) = BURSTFS_ADDR(ret_val) + \
						BURSTFS_OFFSET(start_key) - BURSTFS_OFFSET(ret_key);
				BURSTFS_OFFSET(ret_out_key) = BURSTFS_OFFSET(start_key);

				add_kv(out_key, out_key_len, out_val,\
					out_val_len, tmp_records_cnt, tmp_out_cap, \
						ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);

				if (to_ret == 1) {
					return 0;
				}

				/*start next half*/
				BURSTFS_OFFSET(start_key) = BURSTFS_OFFSET(save_next_ret_key);
				leveldb_iter_next(iter);
				return handle_next_half(iter,\
						start_key, end_key, \
							 out_key, out_key_len, \
								out_val, out_val_len, tmp_records_cnt, \
									tmp_out_cap);


			} else {
				/*	pre_start,...........,pre_end; (start_f)..............(start_e)
				 	 	 	 	 					start			end
				 	 	 	 	  	 	 	 	 	 	 	 	 	 		 */

				/*	pre_start,...........,pre_end; (start_f)..............(start_e)
													start								end
				 	 	 	 	  	 	 	 	 	 	 	 	 	 		 */
				// directly handle the next
				leveldb_iter_next(iter);
				return handle_next_half(iter,\
						start_key, end_key, \
							out_key, out_key_len, \
								out_val, out_val_len, tmp_records_cnt, \
									tmp_out_cap);
			}
		}

	}


	return 0;
}

int handle_next_half(leveldb_iterator_t *iter,\
		char *start_key, char *end_key, \
			char ***out_key, int **out_key_len, \
				char ***out_val, int **out_val_len, int *tmp_records_cnt, \
					int *tmp_out_cap) {
	const char *ret_key, *ret_val;

	long tmp_key_len, tmp_val_len;
	ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
	ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);

	if (BURSTFS_OFFSET(ret_key)\
			> BURSTFS_OFFSET(end_key)) {
		/*	(start)........end....(start_f)...(end_f),........
					 			 */
		return 0;
	} else {
		if (BURSTFS_OFFSET(end_key) <=\
				BURSTFS_OFFSET(ret_key) + BURSTFS_LEN(ret_val) - 1) {
			/* search between start and end*/
			/*	(start).........end............
						start_f  		(end_f)	 */

			char *ret_out_key = malloc(tmp_key_len);
			char *ret_out_val = malloc(tmp_val_len);
			memcpy(ret_out_key, ret_key, tmp_key_len);
			memcpy(ret_out_val, ret_val, tmp_val_len);

			BURSTFS_LEN(ret_out_val) = BURSTFS_OFFSET(end_key)\
				- BURSTFS_OFFSET(ret_key) +1;
			BURSTFS_ADDR(ret_out_val) = BURSTFS_ADDR(ret_val)\
					+ BURSTFS_OFFSET(ret_key) - BURSTFS_OFFSET(start_key);

			add_kv(out_key, out_key_len, out_val,\
				out_val_len, tmp_records_cnt, tmp_out_cap, \
					ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);

			return 0;

		} else {
			/*	(start).......................end......
						start_f 	end_f		 			 */

			int flag = 0;

			char *ret_out_key = malloc(tmp_key_len);
			char *ret_out_val = malloc(tmp_val_len);
		/*	printf("here, ret_key offset is %ld, addr is %ld, len is %ld\n",\
					BURSTFS_OFFSET(ret_key), BURSTFS_ADDR(ret_val),\
						BURSTFS_LEN(ret_val));
			fflush(stdout); */
			memcpy(ret_out_key, ret_key, tmp_key_len);
			memcpy(ret_out_val, ret_val, tmp_val_len);
			add_kv(out_key, out_key_len, out_val,\
				out_val_len, tmp_records_cnt, tmp_out_cap, \
					ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);
			while (1) {

				leveldb_iter_next(iter);

				if (!leveldb_iter_valid(iter)) {
					/*	(start).............(end_f),........
								start_f 			end of file  (cur_start_f) */
					break; /*end_key is beyond the size of database*/
				}

				ret_key = leveldb_iter_key(iter, (size_t *)&tmp_key_len);
				if (!ret_key)
					return MDHIM_DB_ERROR;

				ret_val = leveldb_iter_value(iter, (size_t *)&tmp_val_len);
				if (!ret_val)
					return MDHIM_DB_ERROR;

				if (BURSTFS_FID(ret_key) != BURSTFS_FID(start_key)) {
					break;
				}

				if (BURSTFS_OFFSET(ret_key) > BURSTFS_OFFSET(end_key)) {
					/*	(start).............(end_f),........
								start_f 			end  current_start_f */
					break;
				}

				if (BURSTFS_OFFSET(ret_key) + BURSTFS_LEN(ret_val) - 1 >=\
						BURSTFS_OFFSET(end_key)) {
					/*	(start)............................end_f),........
								start_f 			end*/
					flag = 1;
					break;
				}
				/*	(start).............(end_f),........
							start_f 			end*/
				char *ret_out_key = malloc(tmp_key_len);
				char *ret_out_val = malloc(tmp_val_len);
			/*	printf("here, ret_key offset is %ld, addr is %ld, len is %ld\n",\
						BURSTFS_OFFSET(ret_key), BURSTFS_ADDR(ret_val), \
							BURSTFS_LEN(ret_val));
				fflush(stdout); */
				memcpy(ret_out_key, ret_key, tmp_key_len);
				memcpy(ret_out_val, ret_val, tmp_val_len);
				add_kv(out_key, out_key_len, out_val,\
					out_val_len, tmp_records_cnt, tmp_out_cap, \
						ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);
			}

			if (flag == 1) {
				/*	(start)............................end_f),........
							start_f 			end*/

				char *ret_out_key = malloc(tmp_key_len);
				char *ret_out_val = malloc(tmp_val_len);

			/*	printf("finally, ret_key offset is %ld, addr is %ld, len is %ld\n",\
						BURSTFS_OFFSET(ret_key), BURSTFS_ADDR(ret_val), \
						BURSTFS_LEN(ret_val)); */
				memcpy(ret_out_key, ret_key, tmp_key_len);
				memcpy(ret_out_val, ret_val, tmp_val_len);

				BURSTFS_LEN(ret_out_val) = BURSTFS_OFFSET(end_key)\
						- BURSTFS_OFFSET(ret_key) + 1;
				BURSTFS_ADDR(ret_out_val) = BURSTFS_ADDR(ret_val);
				add_kv(out_key, out_key_len, out_val,\
					out_val_len, tmp_records_cnt, tmp_out_cap, \
						ret_out_key, ret_out_val, tmp_key_len, tmp_val_len);

			}
			return 0;

		}
	}

}

int add_kv(char ***out_key, int **out_key_len, char ***out_val,\
		int **out_val_len, int *tmp_records_cnt, int *tmp_out_cap, \
			char *ret_key, char *ret_val, int key_len, int val_len) {

	if (*tmp_records_cnt == *tmp_out_cap) {
		*out_key = (char **)realloc(*out_key, 2 * (*tmp_out_cap) * sizeof(char *));
		*out_val = (char **)realloc(*out_val, 2 * (*tmp_out_cap) * sizeof(char *));
		*out_key_len = (int *)realloc(*out_key_len, 2 * (*tmp_out_cap) * sizeof(int));
		*out_val_len = (int *)realloc(*out_val_len, 2 * (*tmp_out_cap) * sizeof(int));
		*tmp_out_cap *= 2;
	}

	(*out_key)[*tmp_records_cnt] = ret_key;
	(*out_val)[*tmp_records_cnt] = ret_val;
	(*out_key_len)[*tmp_records_cnt] = key_len;
	(*out_val_len)[*tmp_records_cnt] = val_len;

	*tmp_records_cnt = *tmp_records_cnt + 1;
	return 0;
}
