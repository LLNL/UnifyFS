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

/*
 * DB usage options. 
 * Location and name of DB, type of DataSotre primary key type,
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "mdhim_options.h"

// Default path to a local path and name, levelDB=2, int_key_type=1, yes_create_new=1
// and debug=1 (mlog_CRIT)

#define MANIFEST_FILE_NAME "/mdhim_manifest_"

struct mdhim_options_t *mdhim_options_init()
{
	struct mdhim_options_t* opts;
	opts = malloc(sizeof(struct mdhim_options_t));
    
	opts->db_path = "./";
	opts->db_name = "mdhimTstDB-";
	opts->manifest_path = NULL;
	opts->db_type = 2;
	opts->db_key_type = 1;
	opts->db_create_new = 1;
	opts->db_value_append = MDHIM_DB_OVERWRITE;
	
	opts->db_host = "localhost";
	opts->dbs_host = "localhost";
	opts->db_user = "test";
	opts->db_upswd = "pass";
	opts->dbs_user = "test";
	opts->dbs_upswd = "pass";		
	
        
	opts->debug_level = 1;
        opts->rserver_factor = 1;
        opts->max_recs_per_slice = 100000;
	opts->db_paths = NULL;
	opts->num_paths = 0;
	opts->num_wthreads = 1;

	set_manifest_path(opts, "./");
	return opts;
}

int check_path_length(mdhim_options_t* opts, char *path) {
	int path_len;
	int ret = 0;

	path_len = strlen(path) + 1;
	if (((!opts->db_name && path_len < PATH_MAX) || 
	     ((path_len + strlen(opts->db_name)) < PATH_MAX)) &&
	    (path_len + strlen(MANIFEST_FILE_NAME)) < PATH_MAX) {
		ret = 1;
	} else {
		printf("Path: %s exceeds: %d bytes, so it won't be used\n", path, PATH_MAX);
	}

	return ret;
}

void set_manifest_path(mdhim_options_t* opts, char *path) {
	char *manifest_path;
	int path_len = 0;

	if (opts->manifest_path) {
	  free(opts->manifest_path);
	  opts->manifest_path = NULL;
	}

	path_len = strlen(path) + strlen(MANIFEST_FILE_NAME) + 1;
	manifest_path = malloc(path_len);
	sprintf(manifest_path, "%s%s", path, MANIFEST_FILE_NAME);
	opts->manifest_path = manifest_path;
}

void mdhim_options_set_login_c(mdhim_options_t* opts, char* db_hl, char *db_ln, char *db_pw, char *dbs_hl, char *dbs_ln, char *dbs_pw){
	opts->db_host = db_hl;
	opts->db_user = db_ln;
	opts->db_upswd = db_pw;
	opts->dbs_host = dbs_hl;	
	opts->dbs_user = dbs_ln;
	opts->dbs_upswd = dbs_pw;
	
}
void mdhim_options_set_db_path(mdhim_options_t* opts, char *path)
{
	int ret;

	if (!path) {
		return;
	}

	ret = check_path_length(opts, path);
	if (ret) {
		opts->db_path = path;
		set_manifest_path(opts, path);
	}
};

void mdhim_options_set_db_paths(struct mdhim_options_t* opts, char **paths, int num_paths)
{
	int i = 0;
	int ret;
	int verified_paths = -1;

	if (num_paths <= 0) {
		return;
	}

	opts->db_paths = malloc(sizeof(char *) * num_paths);
	for (i = 0; i < num_paths; i++) {
		if (!paths[i]) {
			continue;
		}

		ret = check_path_length(opts, paths[i]);
		if (!ret) {
			continue;
		}
		if (!i) {
			set_manifest_path(opts, paths[i]);
		}

		verified_paths++;		
		opts->db_paths[verified_paths] = malloc(strlen(paths[i]) + 1);
		sprintf(opts->db_paths[verified_paths], "%s", paths[i]);
	}

	opts->num_paths = ++verified_paths;
};

void mdhim_options_set_db_name(mdhim_options_t* opts, char *name)
{
	opts->db_name = name;
};

void mdhim_options_set_db_type(mdhim_options_t* opts, int type)
{
	opts->db_type = type;
};

void mdhim_options_set_key_type(mdhim_options_t* opts, int key_type)
{
	opts->db_key_type = key_type;
};

void mdhim_options_set_create_new_db(mdhim_options_t* opts, int create_new)
{
	opts->db_create_new = create_new;
};

void mdhim_options_set_debug_level(mdhim_options_t* opts, int dbug)
{
	opts->debug_level = dbug;
};

void mdhim_options_set_value_append(mdhim_options_t* opts, int append)
{
	opts->db_value_append = append;
};

void mdhim_options_set_server_factor(mdhim_options_t* opts, int server_factor)
{
	opts->rserver_factor = server_factor;
};

void mdhim_options_set_max_recs_per_slice(mdhim_options_t* opts, uint64_t max_recs_per_slice)
{
	opts->max_recs_per_slice = max_recs_per_slice;
};

void mdhim_options_set_num_worker_threads(mdhim_options_t* opts, int num_wthreads)
{
	if (num_wthreads > 0) {
		opts->num_wthreads = num_wthreads;
	}
};

void mdhim_options_destroy(mdhim_options_t *opts) {
	int i;

	for (i = 0; i < opts->num_paths; i++) {
		free(opts->db_paths[i]);
	}

	free(opts->manifest_path);
	free(opts);
};
