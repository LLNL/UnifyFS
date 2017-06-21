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

#include <stdio.h>
#include "burstfs_debug.h"
#include "burstfs_const.h"

FILE *dbg_stream = NULL;
char dbg_line[GEN_STR_LEN] = {0};

int dbg_open(char *fname) {
	 dbg_stream = fopen(fname, "a");
	 if (dbg_stream == NULL) {
		dbg_stream = stderr;
		return ULFS_ERROR_DBG;
	 }
	 else {
		return ULFS_SUCCESS;
	}

}

int dbg_close() {
	if (dbg_stream == NULL)
		return ULFS_ERROR_DBG;
	else {
		if (fclose(dbg_stream)== 0)
			return ULFS_SUCCESS;
		return ULFS_ERROR_DBG;

	}
}
