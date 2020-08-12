/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

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
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *	All rights reserved.
 *
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "partitioner.h"

#include "unifyfs_metadata_mdhim.h"

struct timeval calslicestart, calsliceend;
double calslicetime = 0;
struct timeval rangehashstart, rangehashend;
double rangehashtime = 0;
struct timeval serhashstart, serhashend;
double serhashtime = 0; 
unsigned long meta_pair[2] = {0};
//Global hashtable for alphabet used in partitioner algorithm
struct mdhim_char *mdhim_alphabet = NULL;

/**
 * delete_alphabet
 * Deletes the alphabet hash table
 */
void delete_alphabet() {
	struct mdhim_char *cur_char, *tmp;
	HASH_ITER(hh, mdhim_alphabet, cur_char, tmp) {
		HASH_DEL(mdhim_alphabet, cur_char);  /*delete it (mdhim_alphabet advances to next)*/
		free(cur_char);            /* free it */
	}

	mdhim_alphabet = NULL;
}

long double get_str_num(void *key, uint32_t key_len) {
  int id, i;
  struct mdhim_char *mc;
  long double str_num;

  str_num = 0;
  //Iterate through each character to perform the algorithm mentioned above
  for (i = 0; i < key_len; i++) {
    //Ignore null terminating char
    if (i == key_len - 1 && ((char *)key)[i] == '\0') {
      break;
    }
    
    id = (int) ((char *)key)[i];
    HASH_FIND_INT(mdhim_alphabet, &id, mc);
    str_num += mc->pos * powl(2, MDHIM_ALPHABET_EXPONENT * -(i + 1));
  }

  return str_num;
}

/* Allocate a copy of a key and return it. The returned key must be freed. */
void* copy_unifyfs_key(void* key, uint32_t key_len)
{
    void* key_copy = malloc((size_t)key_len);
    memcpy(key_copy, key, (size_t)key_len);
    return key_copy;
}

uint64_t get_byte_num(void *key, uint32_t key_len) {
	uint64_t byte_num;

	byte_num = *((long *)(((char *)key)+sizeof(long)));
	return byte_num;
}

void partitioner_init() {
	// Create the alphabet for string keys
	build_alphabet();
}

/*
 * partitioner_release
 * Releases memory in use by the partitioner
 *
 */
void partitioner_release() {
	delete_alphabet();
	mdhim_alphabet = NULL;
}

/**
 * add_char
 * Adds a character to our alphabet hash table
 *
 * @param id      The id of our entry (the ascii code of the character)
 * @param pos     The value of our entry (the position of the character in our alphabet)
 */
void add_char(int id, int pos) {
	struct mdhim_char *mc;

	//Create a new mdhim_char to hold our entry
	mc = malloc(sizeof(struct mdhim_char));

	//Set the mdhim_char
	mc->id = id;
	mc->pos = pos;

	//Add it to the hash table
	HASH_ADD_INT(mdhim_alphabet, id, mc);    

	return;
}

/**
 * build_alphabet
 * Creates our ascii based alphabet and inserts each character into a uthash table
 */
void build_alphabet() {
	char c;
	int i, indx;

        /* Index of the character in the our alphabet
	   This is to number each character we care about so we can map 
	   a string to a range server 

	   0 - 9 have indexes 0 - 9
	   A - Z have indexes 10 - 35
	   a - z have indexes 36 - 61
	*/
	indx = 0;

	//Start with numbers 0 - 9
	c = '0';	
	for (i = (int) c; i <= (int) '9'; i++) {
		add_char(i, indx);
		indx++;
	}

	//Next deal with A-Z
	c = 'A';	
	for (i = (int) c; i <= (int) 'Z'; i++) {
		add_char(i, indx);
		indx++;
	}

        //Next deal with a-z
	c = 'a';	
	for (i = (int) c; i <= (int) 'z'; i++) {
		add_char(i, indx);
		indx++;
	}

	return;
}

void _add_to_rangesrv_list(rangesrv_list **list, rangesrv_info *ri) {
	rangesrv_list *list_p, *entry;

	entry = malloc(sizeof(rangesrv_list));
	entry->ri = ri;
	entry->next = NULL;
	if (!*list) {
		*list = entry;	
	} else {
		list_p = *list;
		if (list_p->ri == ri)	return;
		while (list_p->next) {
			list_p = list_p->next;
			if (list_p->ri == ri) return;
		}

		list_p->next = entry;
	}

	return;
}

/**
 * verify_key
 * Determines whether the given key is a valid key or not
 *
 * @param key      the key to check
 * @param key_len  the length of the key
 * @param key_type the type of the key
 *
 * @return        MDHIM_ERROR if the key is not valid, otherwise the MDHIM_SUCCESS
 */
int verify_key(struct index_t *index, void *key, 
	       int key_len, int key_type) {
	int i;
	int id;
	struct mdhim_char *mc;
	uint64_t ikey = 0;
	uint64_t size_check;

	if (!key) {
	  return MDHIM_ERROR;
	}

	if (key_len > MAX_KEY_LEN) {
		return MDHIM_ERROR;
	}
	if (key_type == MDHIM_STRING_KEY) {
		for (i = 0; i < key_len; i++) {
			//Ignore null terminating char
			if (i == key_len - 1 && ((char *)key)[i] == '\0') {
				break;
			}
			
			id = (int) ((char *)key)[i];
			HASH_FIND_INT(mdhim_alphabet, &id, mc);
			if (!mc) {
				return MDHIM_ERROR;
			}
		}
	} 

	if (key_type == MDHIM_INT_KEY) {
		ikey = *(uint32_t *)key;
	} else if (key_type == MDHIM_LONG_INT_KEY) {
		ikey = *(uint64_t *)key;
	} else if (key_type == MDHIM_FLOAT_KEY) {
		ikey = *(float *)key;
	} else if (key_type == MDHIM_DOUBLE_KEY) {
		ikey = *(double *)key;
	}

	size_check = ikey/index->mdhim_max_recs_per_slice;
	if (size_check >= MDHIM_MAX_SLICES) {
		mlog(MDHIM_CLIENT_CRIT, "Error - Not enough slices for this key." 
		     "  Try increasing the slice size.");
		return MDHIM_ERROR;
	}
	
	return MDHIM_SUCCESS;
}

int is_float_key(int type) {
	int ret = 0;

	if (type == MDHIM_STRING_KEY) {
		ret = 1;
	} else if (type == MDHIM_FLOAT_KEY) {
		ret = 1;
	} else if (type == MDHIM_DOUBLE_KEY) {
		ret = 1;
	} else if (type == MDHIM_INT_KEY) {
		ret = 0;
	} else if (type == MDHIM_LONG_INT_KEY) {
		ret = 0;
	} else if (type == MDHIM_BYTE_KEY) {
		ret = 1;
	} 

	return ret;
}

/**
 * get_slice_num
 *
 * gets the slice number from a key
 * slice is a portion of the range served by MDHIM
 * each range server servers many slices of the range
 * @param md        main MDHIM struct
 * @param key       pointer to the key to find the range server of
 * @param key_len   length of the key
 * @return the slice number or 0 on error
 */
int get_slice_num(struct mdhim_t *md, struct index_t *index, void *key, int key_len) {
	//The number that maps a key to range server (dependent on key type)
	//	printf("getting slice num\n");
	fflush(stdout);
	int slice_num;
	uint64_t key_num;
	//The range server number that we return
	float fkey;
	double dkey;
	int ret;
	long double map_num;
	uint64_t total_keys;
	int key_type = index->key_type;
	//The last key number that can be represented by the number of slices and the slice size
	total_keys = MDHIM_MAX_SLICES *  index->mdhim_max_recs_per_slice;

	//Make sure this key is valid
	if ((ret = verify_key(index, key, key_len, key_type)) != MDHIM_SUCCESS) {
		mlog(MDHIM_CLIENT_INFO, "Rank: %d - Invalid key given", 
		     md->mdhim_rank);
		return MDHIM_ERROR;
	}

	//Perform key dependent algorithm to get the key in terms of the ranges served
	switch(key_type) {
	case MDHIM_INT_KEY:
		key_num = *(uint32_t *) key;

		break;
	case MDHIM_LONG_INT_KEY:
		key_num = *(uint64_t *) key;

		break;
	case MDHIM_BYTE_KEY:
		/* Algorithm used		   
		   1. Iterate through each byte
		   2. Transform each byte into a floating point number
		   3. Add this floating point number to map_num
		   4. Multiply this number times the total number of keys to get the number 
		      that represents the position in a range

		   For #2, the transformation is as follows:
		   
		   Take the position of the character in the mdhim alphabet 
                   times 2 raised to 8 * -(i + 1) 
		   where i is the current iteration in the loop
		*/		
 
                //Used for calculating the range server to use for this string
	  //		map_num = 0;
	  //		map_num = get_byte_num(key, key_len);	
	  //		key_num = floorl(map_num * total_keys);
		key_num = get_byte_num(key, key_len);	
//		printf("key_num is: %ld\n", key_num);
//		fflush(stdout);
		break;
	case MDHIM_FLOAT_KEY:
		//Convert the key to a float
		fkey = *((float *) key);
		fkey = floor(fabsf(fkey));
		key_num = fkey;

		break;
	case MDHIM_DOUBLE_KEY:
		//Convert the key to a double
		dkey = *((double *) key);
		dkey = floor(fabs(dkey));
		key_num = dkey;

		break;
	case MDHIM_STRING_KEY:
		/* Algorithm used
		   
		   1. Iterate through each character
		   2. Transform each character into a floating point number
		   3. Add this floating point number to map_num
		   4. Multiply this number times the total number of keys to get the number 
		      that represents the position in a range

		   For #2, the transformation is as follows:
		   
		   Take the position of the character in the mdhim alphabet 
                   times 2 raised to the MDHIM_ALPHABET_EXPONENT * -(i + 1) 
		   where i is the current iteration in the loop
		*/		
 
                //Used for calculating the range server to use for this string
		map_num = 0;
		map_num = get_str_num(key, key_len);	
		key_num = floorl(map_num * total_keys);

		break;
    case MDHIM_UNIFYFS_KEY:
        /* Use only the gfid portion of the key, which ensures all extents
         * for the same file hash to the same server */
        key_num = (uint64_t) UNIFYFS_KEY_FID(key);
        break;
	default:
		return 0;
	}


	/* Convert the key to a slice number  */
	slice_num = key_num/index->mdhim_max_recs_per_slice;

	//Return the slice number
	return slice_num;
}

/**
 * get_range_server_by_slice
 *
 * gets the range server that handles the key given
 * @param md        main MDHIM struct
 * @param slice     the slice number
 * @return the rank of the range server or NULL on error
 */
rangesrv_info *get_range_server_by_slice(struct mdhim_t *md, struct index_t *index, int slice) {
	//The number that maps a key to range server (dependent on key type)
	uint32_t rangesrv_num;
	//The range server number that we return
	rangesrv_info *ret_rp;

	if (index->num_rangesrvs == 1) {
		rangesrv_num = 1;
	} else {
		rangesrv_num = slice % index->num_rangesrvs;
		rangesrv_num++;
	}

	//Find the range server number in the hash table
	ret_rp = NULL;
	HASH_FIND_INT(index->rangesrvs_by_num, &rangesrv_num, ret_rp);

	//Return the rank
	return ret_rp;
}

/**
 * get_range_servers
 *
 * gets the range server that handles the key given
 * @param md        main MDHIM struct
 * @param key       pointer to the key to find the range server of
 * @param key_len   length of the key
 * @return the rank of the range server or NULL on error
 */
rangesrv_list *get_range_servers(struct mdhim_t *md, struct index_t *index, 
				 void *key, int key_len) {
	//The number that maps a key to range server (dependent on key type)
	int slice_num;
	//The range server number that we return
	rangesrv_info *ret_rp;
	rangesrv_list *rl;

	if ((slice_num = get_slice_num(md, index, key, key_len)) == MDHIM_ERROR) {
		return NULL;
	}

	ret_rp = get_range_server_by_slice(md, index, slice_num);       
	rl = NULL;
	_add_to_rangesrv_list(&rl, ret_rp);

	//Return the range server list
	return rl;
}

struct mdhim_stat *get_next_slice_stat(struct mdhim_t *md, struct index_t *index, 
				       int slice_num) {
	struct mdhim_stat *stat, *tmp, *next_slice;

	next_slice = NULL;

	//Iterate through the stat hash entries to find the slice 
	//number next after the given slice number
	HASH_ITER(hh, index->stats, stat, tmp) {	
		if (!stat) {
			continue;
		}
		
		if (stat->key > slice_num && !next_slice) {
			next_slice = stat;
		} else if (next_slice && stat->key > slice_num && stat->key < next_slice->key) {
			next_slice = stat;
		}
	}

	return next_slice;
}

struct mdhim_stat *get_prev_slice_stat(struct mdhim_t *md, struct index_t *index, 
				       int slice_num) {
	struct mdhim_stat *stat, *tmp, *prev_slice;

	prev_slice = NULL;

	//Iterate through the stat hash entries to find the slice 
	//number next after the given slice number
	HASH_ITER(hh, index->stats, stat, tmp) {	
		if (!stat) {
			continue;
		}
		
		if (stat->key < slice_num && !prev_slice) {
			prev_slice = stat;
		} else if (prev_slice && stat->key < slice_num && stat->key > prev_slice->key) {
			prev_slice = stat;
		}
	}

	return prev_slice;
}

struct mdhim_stat *get_last_slice_stat(struct mdhim_t *md, struct index_t *index) {
	struct mdhim_stat *stat, *tmp, *last_slice;

	last_slice = NULL;

	//Iterate through the stat hash entries to find the slice 
	//number next after the given slice number
	HASH_ITER(hh, index->stats, stat, tmp) {	
		if (!stat) {
			continue;
		}
		
		if (!last_slice) {
			last_slice = stat;
		} else if (stat->key > last_slice->key) {
			last_slice = stat;
		}
	}

	return last_slice;
}

struct mdhim_stat *get_first_slice_stat(struct mdhim_t *md, struct index_t *index) {
	struct mdhim_stat *stat, *tmp, *first_slice;

	first_slice = NULL;

	//Iterate through the stat hash entries to find the slice 
	//number next after the given slice number
	HASH_ITER(hh, index->stats, stat, tmp) {	
		if (!stat) {
			continue;
		}
		
		if (!first_slice) {
			first_slice = stat;
		} else if (stat->key < first_slice->key) {
			first_slice = stat;
		}
	}

	return first_slice;
}

int get_slice_from_fstat(struct mdhim_t *md, struct index_t *index, 
			 int cur_slice, long double fstat, int op) {
	int slice_num = 0;
	struct mdhim_stat *cur_stat, *new_stat;

	if (!index->stats) {
		return 0;
	}

	//Get the stat struct for our current slice
	HASH_FIND_INT(index->stats, &cur_slice, cur_stat);

	switch(op) {
	case MDHIM_GET_NEXT:
		slice_num = cur_slice;
		break;
	case MDHIM_GET_PREV:
		if (cur_stat && *(long double *)cur_stat->min < fstat) {
			slice_num = cur_slice;
			goto done;
		} else {
			new_stat = get_prev_slice_stat(md, index, cur_slice);
			goto new_stat;
		}

		break;
	case MDHIM_GET_FIRST:
		new_stat = get_first_slice_stat(md, index);
		goto new_stat;
		break;
	case MDHIM_GET_LAST:
		new_stat = get_last_slice_stat(md, index);
		goto new_stat;
		break;
	default:
		slice_num = 0;
		break;
	}
	
done:
	return slice_num;

new_stat:
	if (new_stat) {
		return new_stat->key;
	} else {
		return 0;
	}
}

int get_slice_from_istat(struct mdhim_t *md, struct index_t *index, 
			 int cur_slice, uint64_t istat, int op) {
	int slice_num = 0;
	struct mdhim_stat *cur_stat, *new_stat;

	if (!index->stats) {
		return 0;
	}

	new_stat = cur_stat = NULL;
	//Get the stat struct for our current slice
	HASH_FIND_INT(index->stats, &cur_slice, cur_stat);

	switch(op) {
	case MDHIM_GET_NEXT:
		if (cur_stat && *(uint64_t *)cur_stat->max > istat && 
		    *(uint64_t *)cur_stat->min <= istat) {
			slice_num = cur_slice;
			goto done;
		} else {		
			new_stat = get_next_slice_stat(md, index, cur_slice);
			goto new_stat;
		}

		break;
	case MDHIM_GET_PREV:
		if (cur_stat && *(uint64_t *)cur_stat->min < istat && 
		    *(uint64_t *)cur_stat->max >= istat ) {
			slice_num = cur_slice;
			goto done;
		} else {
			new_stat = get_prev_slice_stat(md, index, cur_slice);
			goto new_stat;
		}

		break;
	case MDHIM_GET_FIRST:
		new_stat = get_first_slice_stat(md, index);
		goto new_stat;
		break;
	case MDHIM_GET_LAST:
		new_stat = get_last_slice_stat(md, index);
		goto new_stat;
		break;
	default:
		slice_num = 0;
		break;
	}
	
done:
	return slice_num;

new_stat:
	if (new_stat) {
		return new_stat->key;
	} else {
		return 0;
	}
}

/* Iterate through the multi-level hash table in index->stats to find the range servers
   that could have the key */
rangesrv_list *get_rangesrvs_from_istat(struct mdhim_t *md, struct index_t *index, 
					uint64_t istat, int op) {
	struct mdhim_stat *cur_rank, *cur_stat, *tmp, *tmp2;
	rangesrv_list *head, *lp, *entry;
	int slice_num = 0;
	unsigned int num_slices;
	unsigned int i;

	if (!index->stats) {
		return 0;
	}

	cur_stat = cur_rank = NULL;
	head = lp = entry = NULL;
	HASH_ITER(hh, index->stats, cur_rank, tmp) {
		num_slices = HASH_COUNT(cur_rank->stats);
		i = 0;
		HASH_ITER(hh, cur_rank->stats, cur_stat, tmp2) {
			if (cur_stat->num <= 0) {
				continue;
			}
			
			slice_num = -1;
			switch(op) {
			case MDHIM_GET_NEXT:
				if (cur_stat && *(uint64_t *)cur_stat->max > istat && 
				    *(uint64_t *)cur_stat->min - 1 <= istat) {
					slice_num = cur_stat->key;
				} 

				break;
			case MDHIM_GET_PREV:
				if (cur_stat && *(uint64_t *)cur_stat->min < istat && 
				    *(uint64_t *)cur_stat->max + 1 >= istat ) {
					slice_num = cur_stat->key;
				} 
				
				break;
			case MDHIM_GET_FIRST:
				if (!i) {
					slice_num = cur_stat->key;
				}
				break;
			case MDHIM_GET_LAST:
				if (i == num_slices - 1) {
					slice_num = cur_stat->key;
				}
				break;
			case MDHIM_GET_EQ:
				if (cur_stat && *(uint64_t *)cur_stat->max >= istat && 
				    *(uint64_t *)cur_stat->min <= istat) {
					slice_num = cur_stat->key;
				} 

				break;
			default:
				slice_num = 0;
				break;
			}
			
			if (slice_num < 0) {
				continue;
			}

			entry = malloc(sizeof(rangesrv_list));
			memset(entry, 0, sizeof(rangesrv_list));
			HASH_FIND_INT(index->rangesrvs_by_rank, &cur_rank->key, entry->ri);
			if (!entry->ri) {
				free(entry);
				continue;
			}

			if (!head) {
				lp = head = entry;				
			} else {
				lp->next = entry;
				lp = lp->next;
			}

			break;
		}
	}

	return head;
}

/* Iterate through the multi-level hash table in index->stats to find the range servers 
   that could have the key */
rangesrv_list *get_rangesrvs_from_fstat(struct mdhim_t *md, struct index_t *index, 
					long double fstat, int op) {
	struct mdhim_stat *cur_rank, *cur_stat, *tmp, *tmp2;
	rangesrv_list *head, *lp, *entry;
	int slice_num = 0;
	unsigned int num_slices;
	unsigned int i;

	if (!index->stats) {
		return 0;
	}

	cur_stat = cur_rank = NULL;
	head = lp = entry = NULL;
	HASH_ITER(hh, index->stats, cur_rank, tmp) {
		num_slices = HASH_COUNT(cur_rank->stats);
		i = 0;
		HASH_ITER(hh, cur_rank->stats, cur_stat, tmp2) {
			if (cur_stat->num <= 0) {
				continue;
			}
			
			slice_num = -1;
			switch(op) {
			case MDHIM_GET_NEXT:
				if (cur_stat && *(long double *)cur_stat->max > fstat && 
				    *(long double *)cur_stat->min - 1.0L <= fstat) {
					slice_num = cur_stat->key;
				} 
				break;
			case MDHIM_GET_PREV:
				if (cur_stat && *(long double *)cur_stat->min < fstat && 
				    *(long double *)cur_stat->max + 1.0L >= fstat ) {
					slice_num = cur_stat->key;
				} 
				
				break;
			case MDHIM_GET_FIRST:
				if (!i) {
					slice_num = cur_stat->key;
				}
				break;
			case MDHIM_GET_LAST:
				if (i == num_slices - 1) {
					slice_num = cur_stat->key;
				}
				break;
			case MDHIM_GET_EQ:
				if (cur_stat && *(long double *)cur_stat->max >= fstat && 
				    *(long double *)cur_stat->min <= fstat) {
					slice_num = cur_stat->key;
				} 
				
				break;
			default:
				slice_num = 0;
				break;
			}
			
			if (slice_num < 0) {
				continue;
			}

			entry = malloc(sizeof(rangesrv_list));
			HASH_FIND_INT(index->rangesrvs_by_rank, &cur_rank->key, entry->ri);
			if (!entry->ri) {
				free(entry);
				continue;
			}

			if (!head) {
				lp = head = entry;				
			} else {
				lp->next = entry;
				lp = lp->next;
			}

			break;
		}
	}

	return head;
}

/**
 * get_range_server_from_stats
 *
 * gets the range server based on the stats acquired from a stat flush
 * @param md        main MDHIM struct
 * @param key       pointer to the key to find the range server of
 * @param key_len   length of the key
 * @param op        operation type (
 * @return the rank of the range server or NULL on error
 */
rangesrv_list *get_range_servers_from_stats(struct mdhim_t *md, struct index_t *index,
					    void *key, int key_len, int op) {
	//The number that maps a key to range server (dependent on key type)
//	printf("get range servers from stats");
//	fflush(stdout);
	int slice_num, cur_slice;
	//The range server number that we return
	rangesrv_info *ret_rp;
	rangesrv_list *rl;
	int float_type = 0;
	long double fstat = 0;
	uint64_t istat = 0;

	if (key && key_len) {
		//Find the slice based on the operation and key value
		if (index->key_type == MDHIM_STRING_KEY) {
			fstat = get_str_num(key, key_len);
		} else if (index->key_type == MDHIM_FLOAT_KEY) {
			fstat = *(float *) key;
		} else if (index->key_type == MDHIM_DOUBLE_KEY) {
			fstat = *(double *) key;
		} else if (index->key_type == MDHIM_INT_KEY) {
			istat = *(uint32_t *) key;
		} else if (index->key_type == MDHIM_LONG_INT_KEY) {
			istat = *(uint64_t *) key;
		} else if (index->key_type == MDHIM_BYTE_KEY) {
			fstat = get_byte_num(key, key_len);
		} 
	}

	//If we don't have any stats info, then return null	
	if (!index->stats) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - No statistics data available." 
		     " Perform a mdhimStatFlush first.", 
		     md->mdhim_rank);
		return NULL;
	}

	if (index->type != LOCAL_INDEX) {

		cur_slice = slice_num = 0;
		float_type = is_float_key(index->key_type);

		//Get the current slice number of our key
		if (key && key_len) {
			cur_slice = get_slice_num(md, index, key, key_len);

			if (cur_slice == MDHIM_ERROR) {

				mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error: could not determine a" 
				     " valid a slice number", 
				     md->mdhim_rank);
				return NULL;
			}	
		} else if (op != MDHIM_GET_FIRST && op != MDHIM_GET_LAST) {
			//If the op is not first or last, then we expect a key

			return NULL;
		}
	
		if (float_type) {
			slice_num = get_slice_from_fstat(md, index, cur_slice, fstat, op);
		} else {	
			slice_num = get_slice_from_istat(md, index, cur_slice, istat, op);
		}

		if (slice_num == MDHIM_ERROR) {
			return NULL;
		}

		ret_rp = get_range_server_by_slice(md, index, slice_num);
		if (!ret_rp) {
			mlog(MDHIM_CLIENT_INFO, "Rank: %d - Did not get a valid range server from" 
			     " get_range_server_by_size", 
			     md->mdhim_rank);
			return NULL;
		}
      
		rl = NULL;
		_add_to_rangesrv_list(&rl, ret_rp);
	} else {
		if (float_type) {
			rl = get_rangesrvs_from_fstat(md, index, fstat, op);
		} else {	
			rl = get_rangesrvs_from_istat(md, index, istat, op);
		}	       
	}

	//Return the range server information
	return rl;
}

rangesrv_list *get_range_servers_from_range(struct mdhim_t *md, struct index_t *index,
					    void *start_key, void *end_key, int key_len) {
	//The number that maps a key to range server (dependent on key type)

	int start_slice, end_slice;
	//The range server number that we return
	rangesrv_info *ret_rp;
	rangesrv_list *rl;

	//If we don't have any stats info, then return null
	if (!index->stats) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - No statistics data available."
		     " Perform a mdhimStatFlush first.",
		     md->mdhim_rank);
		return NULL;
	}

	gettimeofday(&calslicestart, NULL);
	start_slice = get_slice_num(md, index, start_key, key_len);
	if (start_slice == MDHIM_ERROR) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error: could not determine a"
			" valid a slice number",
			md->mdhim_rank);
			return NULL;
	}

	end_slice = get_slice_num(md, index, end_key, key_len);
	if (end_slice == MDHIM_ERROR) {
		mlog(MDHIM_CLIENT_CRIT, "Rank: %d - Error: could not determine a valid a slice number", md->mdhim_rank);
		return NULL;
	}
	gettimeofday(&calsliceend, NULL);
	calslicetime+=1000000*(calsliceend.tv_sec-calslicestart.tv_sec)+calsliceend.tv_usec-calslicestart.tv_usec;

	long i;
	rl = NULL;

	for (i = start_slice; i <= end_slice; i++) {
		struct mdhim_stat *cur_stat;
		gettimeofday(&rangehashstart, NULL);
		HASH_FIND_INT(index->stats, &i, cur_stat);
		gettimeofday(&rangehashend, NULL);
		rangehashtime+=1000000*(rangehashend.tv_sec-rangehashstart.tv_sec)+rangehashend.tv_usec-rangehashstart.tv_usec;
		gettimeofday(&serhashstart, NULL);
		ret_rp = get_range_server_by_slice(md, index, i);
		gettimeofday(&serhashend, NULL);
		serhashtime+=1000000*(serhashend.tv_sec-serhashstart.tv_sec)+serhashend.tv_usec-serhashstart.tv_usec;

		if (!ret_rp) {
			mlog(MDHIM_CLIENT_INFO, "Rank: %d - Did not get a valid range server from"
			     " get_range_server_by_size",
			     md->mdhim_rank);
			return NULL;
		}
		ret_rp->num_recs = 0;
		ret_rp->first_key = NULL;

	}

	for (i = start_slice; i <= end_slice; i++) {
		struct mdhim_stat *cur_stat;
		gettimeofday(&rangehashstart, NULL);
		HASH_FIND_INT(index->stats, &i, cur_stat);
		gettimeofday(&rangehashend, NULL);
		rangehashtime+=1000000*(rangehashend.tv_sec-rangehashstart.tv_sec)+rangehashend.tv_usec-rangehashstart.tv_usec;

		gettimeofday(&serhashstart, NULL);
		ret_rp = get_range_server_by_slice(md, index, i);
		gettimeofday(&serhashend, NULL);
		serhashtime+=1000000*(serhashend.tv_sec-serhashstart.tv_sec)+serhashend.tv_usec-serhashstart.tv_usec;
		if (!ret_rp) {
			mlog(MDHIM_CLIENT_INFO, "Rank: %d - Did not get a valid range server from"
			     " get_range_server_by_size",
			     md->mdhim_rank);
			return NULL;
		}

		ret_rp->num_recs += cur_stat->num;
		if (ret_rp->first_key == NULL) {
			ret_rp->first_key = cur_stat->min;
		}
		else {
			if (unifyfs_compare(ret_rp->first_key, cur_stat->min) > 0 ) {
				ret_rp->first_key = cur_stat->min;
			}
		}	

		_add_to_rangesrv_list(&rl, ret_rp);
	}

	//Return the range server information
	return rl;

}
