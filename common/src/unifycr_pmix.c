/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "unifycr_const.h"
#include "unifycr_log.h"
#include "unifycr_pmix.h"

static int initialized;    // = 0
static int published;      // = 0
static size_t univ_nprocs; // = 0
static pmix_proc_t myproc;

// PMIx host
char myhost[80];

// PMIx keys we use
const char* pmix_key_runstate = "unifycr.runstate";
const char* pmix_key_unifycrd_socket = "unifycrd.socket";
const char* pmix_key_unifycrd_margo_shm = "unifycrd.margo-shm";
const char* pmix_key_unifycrd_margo_svr = "unifycrd.margo-svr";

// initialize PMIx
int unifycr_pmix_init(int* orank,
                      size_t* ouniv)
{
    int rc;
    pmix_value_t value;
    pmix_value_t* valp = &value;
    pmix_proc_t proc;

    if (!initialized) {
        gethostname(myhost, sizeof(myhost));

        /* init PMIx */
        PMIX_PROC_CONSTRUCT(&myproc);
        rc = PMIx_Init(&myproc, NULL, 0);
        if (rc != PMIX_SUCCESS) {
            LOGERR("PMIx_Init failed: %s", PMIx_Error_string(rc));
            return (int)UNIFYCR_FAILURE;
        }

        /* get PMIx universe size */
        PMIX_PROC_CONSTRUCT(&proc);
        (void)strncpy(proc.nspace, myproc.nspace, PMIX_MAX_NSLEN);
        proc.rank = PMIX_RANK_WILDCARD;
        rc = PMIx_Get(&proc, PMIX_UNIV_SIZE, NULL, 0, &valp);
        if (rc != PMIX_SUCCESS) {
            LOGERR("PMIx rank %d: PMIx_Get(UNIV_SIZE) failed: %s",
                   myproc.rank, PMIx_Error_string(rc));
            return (int)UNIFYCR_FAILURE;
        }
        univ_nprocs = (size_t) valp->data.uint32;
        PMIX_VALUE_RELEASE(valp);

        initialized = 1;
    }

    if (orank != NULL) {
        *orank = myproc.rank;
    }
    if (ouniv != NULL) {
        *ouniv = univ_nprocs;
    }

    return (int)UNIFYCR_SUCCESS;
}

// finalize PMIx
int unifycr_pmix_fini(void)
{
    int rc;
    size_t ninfo;
    pmix_info_t* info;
    pmix_data_range_t range;

    rc = (int) UNIFYCR_SUCCESS;

    if (initialized) {

        if (published) {
            /* unpublish everything I published */
            range = PMIX_RANGE_GLOBAL;
            ninfo = 1;
            PMIX_INFO_CREATE(info, ninfo);
            PMIX_INFO_LOAD(&info[0], PMIX_RANGE, &range, PMIX_DATA_RANGE);
            rc = PMIx_Unpublish(NULL, info, ninfo);
            if (rc != PMIX_SUCCESS) {
                LOGERR("PMIx rank %d: PMIx_Unpublish failed: %s",
                       myproc.rank, PMIx_Error_string(rc));
            }
            published = 0;
        }

        /* fini PMIx */
        rc = PMIx_Finalize(NULL, 0);
        if (rc != PMIX_SUCCESS) {
            LOGERR("PMIx rank %d: PMIx_Finalize() failed: %s",
                   myproc.rank, PMIx_Error_string(rc));
            rc = (int) UNIFYCR_FAILURE;
        }
        PMIX_PROC_DESTRUCT(&myproc);
        univ_nprocs = 0;
        initialized = 0;
    }
    return rc;
}

// publish a key-value pair
int unifycr_pmix_publish(const char* key,
                         const char* val)
{
    int rc;
    size_t len, hlen, ninfo;
    pmix_info_t* info;
    pmix_data_range_t range;
    char pmix_key[PMIX_MAX_KEYLEN+1];

    if (!initialized) {
        rc = unifycr_pmix_init(NULL, NULL);
        if (rc != (int)UNIFYCR_SUCCESS) {
            return rc;
        }
    }

    if ((key == NULL) || (val == NULL)) {
        LOGERR("NULL key or value");
        return (int)UNIFYCR_ERROR_INVAL;
    }

    len = strlen(key);
    hlen = strlen(myhost);
    if ((len + hlen) >= sizeof(pmix_key)) {
        LOGERR("length of key (%zd) exceeds max %zd",
               len, sizeof(pmix_key));
        return (int)UNIFYCR_ERROR_INVAL;
    } else {
        memset(pmix_key, 0, sizeof(pmix_key));
        snprintf(pmix_key, sizeof(pmix_key), "%s.%s", key, myhost);
        LOGDBG("publishing key %s", pmix_key);
    }

    /* set key-val and modify publish behavior */
    range = PMIX_RANGE_GLOBAL;
    ninfo = 2;
    PMIX_INFO_CREATE(info, ninfo);
    PMIX_INFO_LOAD(&info[0], pmix_key, val, PMIX_STRING);
    PMIX_INFO_LOAD(&info[1], PMIX_RANGE, &range, PMIX_DATA_RANGE);

    /* try to publish */
    rc = PMIx_Publish(info, ninfo);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Publish failed: %s",
               myproc.rank, PMIx_Error_string(rc));
        rc = (int)UNIFYCR_FAILURE;
    } else {
        published = 1;
        rc = (int)UNIFYCR_SUCCESS;
    }
    /* cleanup */
    PMIX_INFO_FREE(info, ninfo);

    return rc;
}

// lookup a key-value pair
int unifycr_pmix_lookup_common(const char* pmix_key,
                               int keywait,
                               char** oval)
{
    int rc, wait;
    size_t ndir;
    pmix_data_range_t range;
    pmix_info_t* directives;
    pmix_pdata_t* pdata;

    if ((pmix_key == NULL) || (oval == NULL)) {
        LOGERR("NULL key or value");
        return (int)UNIFYCR_ERROR_INVAL;
    }

    /* set key to lookup */
    PMIX_PDATA_CREATE(pdata, 1);
    PMIX_PDATA_LOAD(&pdata[0], &myproc, pmix_key, NULL, PMIX_STRING);

    /* modify lookup behavior */
    wait = 0;
    ndir = 1;
    if (keywait) {
        ndir++;
        wait = 1;
    }
    range = PMIX_RANGE_GLOBAL;
    PMIX_INFO_CREATE(directives, ndir);
    PMIX_INFO_LOAD(&directives[0], PMIX_RANGE, &range, PMIX_DATA_RANGE);
    if (keywait) {
        PMIX_INFO_LOAD(&directives[1], PMIX_WAIT, &wait, PMIX_INT);
    }

    /* try lookup */
    rc = PMIx_Lookup(pdata, 1, directives, ndir);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Lookup(%s) failed: %s",
               myproc.rank, pmix_key, PMIx_Error_string(rc));
        *oval = NULL;
        rc = (int)UNIFYCR_FAILURE;
    } else {
        if (pdata[0].value.data.string != NULL) {
            *oval = strdup(pdata[0].value.data.string);
            rc = (int)UNIFYCR_SUCCESS;
        } else {
            LOGERR("PMIx rank %d: PMIx_Lookup(%s) returned NULL string",
                   myproc.rank, pmix_key);
            *oval = NULL;
            rc = (int)UNIFYCR_FAILURE;
        }
    }
    /* cleanup */
    PMIX_PDATA_FREE(pdata, 1);
    PMIX_INFO_FREE(directives, ndir);

    return rc;
}

int unifycr_pmix_lookup(const char* key,
                        int keywait,
                        char** oval)
{
    int rc;
    char full_key[PMIX_MAX_KEYLEN+1];

    if (!initialized) {
        rc = unifycr_pmix_init(NULL, NULL);
        if (rc != (int)UNIFYCR_SUCCESS) {
            return rc;
        }
    }

    memset(full_key, 0, sizeof(full_key));
    snprintf(full_key, sizeof(full_key), "%s.%s", key, myhost);
    return unifycr_pmix_lookup_common(full_key, keywait, oval);
}

int unifycr_pmix_lookup_remote(const char* host,
                               const char* key,
                               int keywait,
                               char** oval)
{
    int rc;
    char full_key[PMIX_MAX_KEYLEN+1];

    if (!initialized) {
        rc = unifycr_pmix_init(NULL, NULL);
        if (rc != (int)UNIFYCR_SUCCESS) {
            return rc;
        }
    }

    memset(full_key, 0, sizeof(full_key));
    snprintf(full_key, sizeof(full_key), "%s.%s", key, host);
    return unifycr_pmix_lookup_common(full_key, keywait, oval);
}

