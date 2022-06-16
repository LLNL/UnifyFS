/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "unifyfs_const.h"
#include "unifyfs_keyval.h"
#include "unifyfs_log.h"
#include "unifyfs_misc.h"

//#include "config.h"

#if defined(USE_PMIX)
# include <pmix.h>
#elif defined(USE_PMI2)
# include <pmi2.h>
#endif

#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// UnifyFS keys
const char* const key_unifyfsd_socket    = "unifyfsd.socket";
const char* const key_unifyfsd_margo_shm = "unifyfsd.margo-shm";
const char* const key_unifyfsd_margo_svr = "unifyfsd.margo-svr";

// key-value store state
static int kv_initialized; // = 0
static int kv_published;   // = 0
static int kv_myrank = -1;
static int kv_nranks = -1;
static char kv_myhost[80];

// max supported key and value length
static size_t kv_max_keylen; // = 0
static size_t kv_max_vallen; // = 0

#ifndef UNIFYFS_MAX_KV_KEYLEN
# define UNIFYFS_MAX_KV_KEYLEN 256
#endif

#ifndef UNIFYFS_MAX_KV_VALLEN
# define UNIFYFS_MAX_KV_VALLEN 4096
#endif

/* PMI information */
int glb_pmi_rank = -1;
int glb_pmi_size; /* = 0 */

//--------------------- PMI2 K-V Store ---------------------
#if defined(USE_PMI2)
#include <pmi2.h>

static int pmi2_initialized;   // = 0
static int pmi2_need_finalize; // = 0
static int pmi2_has_nameserv;  // = 0

static char pmi2_errstr[64];

static void unifyfs_pmi2_errstr(int rc)
{
    switch (rc) {
    case PMI2_SUCCESS:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Success");
        break;
    case PMI2_FAIL:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Failure");
        break;
    case PMI2_ERR_NOMEM:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Out of Memory");
        break;
    case PMI2_ERR_INIT:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Init Error");
        break;
    case PMI2_ERR_INVALID_ARG:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Arg");
        break;
    case PMI2_ERR_INVALID_KEY:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Key");
        break;
    case PMI2_ERR_INVALID_KEY_LENGTH:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Key Length");
        break;
    case PMI2_ERR_INVALID_VAL:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Val");
        break;
    case PMI2_ERR_INVALID_VAL_LENGTH:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Val Length");
        break;
    case PMI2_ERR_INVALID_LENGTH:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Length");
        break;
    case PMI2_ERR_INVALID_NUM_ARGS:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Num Args");
        break;
    case PMI2_ERR_INVALID_ARGS:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Args");
        break;
    case PMI2_ERR_INVALID_NUM_PARSED:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Num Parsed");
        break;
    case PMI2_ERR_INVALID_KEYVALP:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid KeyVal Ptr");
        break;
    case PMI2_ERR_INVALID_SIZE:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Invalid Size");
        break;
    case PMI2_ERR_OTHER:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "PMI2 Other Error");
        break;
    default:
        snprintf(pmi2_errstr, sizeof(pmi2_errstr), "Unknown PMI2 Error Code");
        break;
    }
}

// initialize PMI2
int unifyfs_pmi2_init(void)
{
    int nprocs, rank, rc, val, len, found;
    int pmi_world_rank = -1;
    int pmi_world_nprocs = -1;

    /* return success if we're already initialized */
    if (pmi2_initialized) {
        return (int)UNIFYFS_SUCCESS;
    }

    kv_max_keylen = PMI2_MAX_KEYLEN;
    kv_max_vallen = PMI2_MAX_VALLEN;

    rc = PMI2_Initialized();
    if (!rc) {
        int spawned, appid;
        rc = PMI2_Init(&spawned, &nprocs, &rank, &appid);
        if (rc != PMI2_SUCCESS) {
            unifyfs_pmi2_errstr(rc);
            LOGERR("PMI2_Init() failed: %s", pmi2_errstr);
            return (int)UNIFYFS_ERROR_PMI;
        }
        pmi_world_rank = rank;
        pmi_world_nprocs = nprocs;
        pmi2_need_finalize = 1;
    }

    if (-1 == pmi_world_rank) {
        rc = PMI2_Job_GetRank(&rank);
        if (rc != PMI2_SUCCESS) {
            unifyfs_pmi2_errstr(rc);
            LOGERR("PMI2_Job_GetRank() failed: %s", pmi2_errstr);
            return (int)UNIFYFS_ERROR_PMI;
        } else {
            pmi_world_rank = rank;
        }
    }

    if (-1 == pmi_world_nprocs) {
        found = 0;
        nprocs = 0;
        rc = PMI2_Info_GetJobAttrIntArray("universeSize", &nprocs, 1,
                                          &len, &found);
        if (found) {
            pmi_world_nprocs = nprocs;
        }
    }

    found = 0;
    val = 0;
    rc = PMI2_Info_GetJobAttrIntArray("hasNameServ", &val, 1,
                                &len, &found);
    if (found) {
        pmi2_has_nameserv = val;
    }

    char pmi_jobid[64];
    memset(pmi_jobid, 0, sizeof(pmi_jobid));
    rc = PMI2_Job_GetId(pmi_jobid, sizeof(pmi_jobid));
    if (rc != PMI2_SUCCESS) {
        unifyfs_pmi2_errstr(rc);
        LOGERR("PMI2_Job_GetId() failed: %s", pmi2_errstr);
        return (int)UNIFYFS_ERROR_PMI;
    }

    kv_myrank = pmi_world_rank;
    kv_nranks = pmi_world_nprocs;

    glb_pmi_rank = kv_myrank;
    glb_pmi_size = kv_nranks;

    LOGDBG("PMI2 Job Id: %s, Rank: %d of %d, hasNameServer=%d", pmi_jobid,
           kv_myrank, kv_nranks, pmi2_has_nameserv);

    pmi2_initialized = 1;

    return (int)UNIFYFS_SUCCESS;
}

// finalize PMI2
static int unifyfs_pmi2_fini(void)
{
    int rc;

    if (pmi2_need_finalize) {
        rc = PMI2_Finalize();
        if (rc != PMI2_SUCCESS) {
            unifyfs_pmi2_errstr(rc);
            LOGERR("PMI2_Finalize() failed: %s", pmi2_errstr);
            return (int)UNIFYFS_ERROR_PMI;
        }
        pmi2_need_finalize = 0;
        pmi2_initialized = 0;
    }

    return (int)UNIFYFS_SUCCESS;
}

// lookup a key-value pair
static int unifyfs_pmi2_lookup(const char* key,
                               char** oval)
{
    int rc, vallen;
    char pmi2_key[PMI2_MAX_KEYLEN] = {0};
    char pmi2_val[PMI2_MAX_VALLEN] = {0};

    if (!pmi2_initialized) {
        return (int)UNIFYFS_ERROR_PMI;
    }

    strncpy(pmi2_key, key, sizeof(pmi2_key));
    vallen = 0;
    rc = PMI2_KVS_Get(NULL, PMI2_ID_NULL, pmi2_key, pmi2_val,
                      sizeof(pmi2_val), &vallen);
    if (rc != PMI2_SUCCESS) {
        unifyfs_pmi2_errstr(rc);
        LOGERR("PMI2_KVS_Get(%s) failed: %s", key, pmi2_errstr);
        return (int)UNIFYFS_ERROR_PMI;
    }

    // HACK: replace '!' with ';' for SLURM PMI2
    // This assumes the value does not actually use "!"
    //
    // At least one version of SLURM PMI2 seems to use ";"
    // characters to separate key/value pairs, so the following:
    //
    // PMI2_KVS_Put("unifyfs.margo-svr", "ofi+tcp;ofi_rxm://ip:port")
    //
    // leads to an error like:
    //
    // slurmstepd: error: mpi/pmi2: no value for key ;ofi_rxm://ip:port; in req
    char* p = pmi2_val;
    while (*p != '\0') {
        if (*p == '!') {
            *p = ';';
        }
        p++;
    }

    *oval = strdup(pmi2_val);
    return (int)UNIFYFS_SUCCESS;
}

// publish a key-value pair
static int unifyfs_pmi2_publish(const char* key,
                                const char* val)
{
    int rc;
    char pmi2_key[PMI2_MAX_KEYLEN] = {0};
    char pmi2_val[PMI2_MAX_VALLEN] = {0};

    if (!pmi2_initialized) {
        return (int)UNIFYFS_ERROR_PMI;
    }

    strncpy(pmi2_key, key, sizeof(pmi2_key));
    strncpy(pmi2_val, val, sizeof(pmi2_val));

    // HACK: replace ';' with '!' for SLURM PMI2
    // This assumes the value does not actually use "!"
    //
    // At least one version of SLURM PMI2 seems to use ";"
    // characters to separate key/value pairs, so the following:
    //
    // PMI2_KVS_Put("unifyfs.margo-svr", "ofi+tcp;ofi_rxm://ip:port")
    //
    // leads to an error like:
    //
    // slurmstepd: error: mpi/pmi2: no value for key ;ofi_rxm://ip:port; in req
    char* p = pmi2_val;
    while (*p != '\0') {
        if (*p == ';') {
            *p = '!';
        }
        p++;
    }

    rc = PMI2_KVS_Put(pmi2_key, pmi2_val);
    if (rc != PMI2_SUCCESS) {
        unifyfs_pmi2_errstr(rc);
        LOGERR("PMI2_KVS_Put(%s) failed: %s", key, pmi2_errstr);
        return (int)UNIFYFS_ERROR_PMI;
    }
    return (int)UNIFYFS_SUCCESS;
}

static int unifyfs_pmi2_fence(void)
{
    /* PMI2_KVS_Fence() is a collective barrier that ensures
     * all previous KVS_Put()s are visible */
    int rc = PMI2_KVS_Fence();
    if (rc != PMI2_SUCCESS) {
        unifyfs_pmi2_errstr(rc);
        LOGERR("PMI2_KVS_Fence() failed: %s", pmi2_errstr);
        return (int)UNIFYFS_ERROR_PMI;
    }
    return (int)UNIFYFS_SUCCESS;
}

#endif // USE_PMI2


//--------------------- PMIx K-V Store ---------------------
#if defined(USE_PMIX)
#include <pmix.h>

static int pmix_initialized; // = 0
static pmix_proc_t pmix_myproc;

#ifndef PMIX_MAX_VALLEN
# define PMIX_MAX_VALLEN UNIFYFS_MAX_KV_VALLEN
#endif

// initialize PMIx
int unifyfs_pmix_init(void)
{
    int rc;
    size_t pmix_univ_nprocs;
    pmix_value_t value;
    pmix_value_t* valp = &value;
    pmix_proc_t proc;

    /* return success if we're already initialized */
    if (pmix_initialized) {
        return (int)UNIFYFS_SUCCESS;
    }

    /* init PMIx */
    PMIX_PROC_CONSTRUCT(&pmix_myproc);
    rc = PMIx_Init(&pmix_myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx_Init() failed: %s", PMIx_Error_string(rc));
        return (int)UNIFYFS_ERROR_PMI;
    }

    kv_max_keylen = PMIX_MAX_KEYLEN;
    kv_max_vallen = PMIX_MAX_VALLEN;

    /* get PMIx job size */
    PMIX_PROC_CONSTRUCT(&proc);
    strlcpy(proc.nspace, pmix_myproc.nspace, PMIX_MAX_NSLEN);
    proc.rank = PMIX_RANK_WILDCARD;

    // Note: we do an extra copy because passing PMIX_JOB_SIZE directly to
    // PMIx_Get() causes gcc 11 to generate a warning due to the fact that
    // PMIX_JOB_SIZE evaluates to a 14 byte char array while pmix_key_t is
    // (at least) 64 bytes.
    pmix_key_t key;
    strcpy(key, PMIX_JOB_SIZE);
    rc = PMIx_Get(&proc, key, NULL, 0, &valp);

    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Get(JOB_SIZE) failed: %s",
               pmix_myproc.rank, PMIx_Error_string(rc));
        return (int)UNIFYFS_ERROR_PMI;
    }
    pmix_univ_nprocs = (size_t) valp->data.uint32;
    LOGDBG("PMIX_JOB_SIZE: %d", pmix_univ_nprocs);

    kv_myrank = pmix_myproc.rank;
    kv_nranks = (int)pmix_univ_nprocs;

    glb_pmi_rank = kv_myrank;
    glb_pmi_size = kv_nranks;

    LOGDBG("PMIX Job Id: %s, Rank: %d of %d", pmix_myproc.nspace,
           kv_myrank, kv_nranks);

    PMIX_VALUE_RELEASE(valp);
    PMIX_PROC_DESTRUCT(&proc);

    pmix_initialized = 1;

    return (int)UNIFYFS_SUCCESS;
}

// finalize PMIx
static int unifyfs_pmix_fini(void)
{
    int rc;
    size_t ninfo;
    pmix_info_t* info;
    pmix_data_range_t range;

    if (kv_published) {
        /* unpublish everything I published */
        range = PMIX_RANGE_GLOBAL;
        ninfo = 1;
        PMIX_INFO_CREATE(info, ninfo);
        PMIX_INFO_LOAD(&info[0], PMIX_RANGE, &range, PMIX_DATA_RANGE);
        rc = PMIx_Unpublish(NULL, info, ninfo);
        if (rc != PMIX_SUCCESS) {
            LOGERR("PMIx rank %d: PMIx_Unpublish() failed: %s",
                   pmix_myproc.rank, PMIx_Error_string(rc));
        }
    }

    /* fini PMIx */
    rc = PMIx_Finalize(NULL, 0);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Finalize() failed: %s",
               pmix_myproc.rank, PMIx_Error_string(rc));
        rc = (int) UNIFYFS_ERROR_PMI;
    } else {
        PMIX_PROC_DESTRUCT(&pmix_myproc);
        pmix_initialized = 0;
        rc = (int) UNIFYFS_SUCCESS;
    }
    return rc;
}

static int unifyfs_pmix_lookup(const char* key,
                               char** oval)
{
    int rc, wait;
    size_t ndir;
    pmix_data_range_t range;
    pmix_info_t* directives;
    pmix_pdata_t* pdata;
    char pmix_key[PMIX_MAX_KEYLEN+1];

    if (!pmix_initialized) {
        return (int)UNIFYFS_ERROR_PMI;
    }

    /* set key to lookup */
    strlcpy(pmix_key, key, sizeof(pmix_key));
    PMIX_PDATA_CREATE(pdata, 1);
    PMIX_PDATA_LOAD(&pdata[0], &pmix_myproc, pmix_key, NULL, PMIX_STRING);

    /* modify lookup behavior */
    wait = 1;
    ndir = 2;
    range = PMIX_RANGE_GLOBAL;
    PMIX_INFO_CREATE(directives, ndir);
    PMIX_INFO_LOAD(&directives[0], PMIX_RANGE, &range, PMIX_DATA_RANGE);
    PMIX_INFO_LOAD(&directives[1], PMIX_WAIT, &wait, PMIX_INT);

    /* try lookup */
    rc = PMIx_Lookup(pdata, 1, directives, ndir);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Lookup(%s) failed: %s",
               pmix_myproc.rank, pmix_key, PMIx_Error_string(rc));
        *oval = NULL;
        rc = (int)UNIFYFS_ERROR_PMI;
    } else {
        if (pdata[0].value.data.string != NULL) {
            *oval = strdup(pdata[0].value.data.string);
            rc = (int)UNIFYFS_SUCCESS;
        } else {
            LOGERR("PMIx rank %d: PMIx_Lookup(%s) returned NULL string",
                   pmix_myproc.rank, pmix_key);
            *oval = NULL;
            rc = (int)UNIFYFS_ERROR_PMI;
        }
    }
    /* cleanup */
    PMIX_PDATA_FREE(pdata, 1);
    PMIX_INFO_FREE(directives, ndir);
    return rc;
}

// publish a key-value pair
static int unifyfs_pmix_publish(const char* key,
                                const char* val)
{
    int rc;
    size_t ninfo;
    pmix_info_t* info;
    pmix_data_range_t range;
    char pmix_key[PMIX_MAX_KEYLEN+1];

    if (!pmix_initialized) {
        return (int)UNIFYFS_ERROR_PMI;
    }

    /* set key-val and modify publish behavior */
    strlcpy(pmix_key, key, sizeof(pmix_key));
    range = PMIX_RANGE_GLOBAL;
    ninfo = 2;
    PMIX_INFO_CREATE(info, ninfo);
    PMIX_INFO_LOAD(&info[0], pmix_key, val, PMIX_STRING);
    PMIX_INFO_LOAD(&info[1], PMIX_RANGE, &range, PMIX_DATA_RANGE);

    /* try to publish */
    rc = PMIx_Publish(info, ninfo);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Publish failed: %s",
               pmix_myproc.rank, PMIx_Error_string(rc));
        rc = (int)UNIFYFS_ERROR_PMI;
    } else {
        rc = (int)UNIFYFS_SUCCESS;
    }
    /* cleanup */
    PMIX_INFO_FREE(info, ninfo);
    return rc;
}

static int unifyfs_pmix_fence(void)
{
    // PMIx_Fence is a collective barrier across all processes in my namespace
    int rc = PMIx_Fence(NULL, 0, NULL, 0);
    if (rc != PMIX_SUCCESS) {
        LOGERR("PMIx rank %d: PMIx_Fence failed: %s",
               pmix_myproc.rank, PMIx_Error_string(rc));
        rc = (int)UNIFYFS_ERROR_PMI;
    } else {
        rc = (int)UNIFYFS_SUCCESS;
    }
    return rc;
}

#endif // USE_PMIX


//--------------------- File system K-V Store ---------------------

static char localfs_kvdir[UNIFYFS_MAX_FILENAME];
static char sharedfs_kvdir[UNIFYFS_MAX_FILENAME];
static char sharedfs_rank_kvdir[UNIFYFS_MAX_FILENAME];
static int have_sharedfs_kvstore; // = 0

static int unifyfs_fskv_init(unifyfs_cfg_t* cfg)
{
    int rc, err;
    struct stat s;

    if (NULL == cfg) {
        LOGERR("NULL config");
        return EINVAL;
    }

    memset(localfs_kvdir, 0, sizeof(localfs_kvdir));
    memset(sharedfs_kvdir, 0, sizeof(sharedfs_kvdir));
    memset(sharedfs_rank_kvdir, 0, sizeof(sharedfs_rank_kvdir));

    // find or create local kvstore directory
    if (NULL == cfg->runstate_dir) {
        LOGERR("local file system k-v store requires cfg.runstate_dir");
        return (int)UNIFYFS_ERROR_BADCONFIG;
    }
    snprintf(localfs_kvdir, sizeof(localfs_kvdir), "%s/kvstore",
             cfg->runstate_dir);
    memset(&s, 0, sizeof(struct stat));
    rc = stat(localfs_kvdir, &s);
    if (rc != 0) {
        if (UNIFYFS_SERVER == cfg->ptype) {
            // try to create it
            rc = mkdir(localfs_kvdir, 0770);
            err = errno;
            if ((rc != 0) && (err != EEXIST)) {
                LOGERR("failed to create local kvstore directory %s - %s",
                       localfs_kvdir, strerror(err));
                return (int)UNIFYFS_ERROR_KEYVAL;
            }
        } else {
            LOGERR("missing local kvstore directory %s", localfs_kvdir);
            return (int)UNIFYFS_ERROR_KEYVAL;
        }
    }

    if (UNIFYFS_SERVER == cfg->ptype) {
        if (NULL != cfg->sharedfs_dir) {
            // find or create shared kvstore directory
            snprintf(sharedfs_kvdir, sizeof(sharedfs_kvdir), "%s/kvstore",
                     cfg->sharedfs_dir);
            memset(&s, 0, sizeof(struct stat));
            rc = stat(sharedfs_kvdir, &s);
            if (rc != 0) {
                // try to create it
                rc = mkdir(sharedfs_kvdir, 0770);
                err = errno;
                if ((rc != 0) && (err != EEXIST)) {
                    LOGERR("failed to create kvstore directory %s - %s",
                           sharedfs_kvdir, strerror(err));
                    return (int)UNIFYFS_ERROR_KEYVAL;
                }
            }

            // find or create rank-specific subdir
            scnprintf(sharedfs_rank_kvdir, sizeof(sharedfs_rank_kvdir), "%s/%d",
                      sharedfs_kvdir, kv_myrank);
            memset(&s, 0, sizeof(struct stat));
            rc = stat(sharedfs_rank_kvdir, &s);
            if (rc != 0) {
                // try to create it
                rc = mkdir(sharedfs_rank_kvdir, 0770);
                err = errno;
                if ((rc != 0) && (err != EEXIST)) {
                    LOGERR("failed to create rank kvstore directory %s - %s",
                           sharedfs_rank_kvdir, strerror(err));
                    return (int)UNIFYFS_ERROR_KEYVAL;
                }
            }
            have_sharedfs_kvstore = 1;
        } else {
            // Server process, but nobody specified the sharedfs dir
            LOGERR("can't create kvstore - sharedfs not specified");
            return (int)UNIFYFS_ERROR_KEYVAL;
        }
    }

    kv_max_keylen = UNIFYFS_MAX_KV_KEYLEN;
    kv_max_vallen = UNIFYFS_MAX_KV_VALLEN;
    return (int)UNIFYFS_SUCCESS;
}

static int unifyfs_fskv_fini(void)
{
    int rc;
    struct stat s;
    struct dirent* de;

    kv_max_keylen = 0;
    kv_max_vallen = 0;

    // cleanup local k-v hierarchy
    memset(&s, 0, sizeof(struct stat));
    rc = stat(localfs_kvdir, &s);
    if (rc == 0) {
        // remove local k-v files
        char kvfile[UNIFYFS_MAX_FILENAME];
        DIR* lkv = opendir(localfs_kvdir);
        if (NULL == lkv) {
            LOGERR("failed to opendir(%s)", localfs_kvdir);
            return (int)UNIFYFS_ERROR_KEYVAL;
        }
        while (NULL != (de = readdir(lkv))) {
            if ((0 == strcmp(".", de->d_name)) ||
                (0 == strcmp("..", de->d_name))) {
                continue;
            }
            memset(kvfile, 0, sizeof(kvfile));
            scnprintf(kvfile, sizeof(kvfile), "%s/%s",
                     localfs_kvdir, de->d_name);
            rc = remove(kvfile);
            if (rc != 0) {
                LOGERR("failed to remove local kvstore entry %s",
                       de->d_name);
            }
        }
        closedir(lkv);

        // remove local k-v root
        rc = rmdir(localfs_kvdir);
        if (rc != 0) {
            LOGERR("failed to remove local kvstore dir %s",
                   sharedfs_rank_kvdir);
            return (int)UNIFYFS_ERROR_KEYVAL;
        }
    }

    if (have_sharedfs_kvstore) {
        // cleanup rank-specific k-v hierarchy
        memset(&s, 0, sizeof(struct stat));
        rc = stat(sharedfs_rank_kvdir, &s);
        if (rc == 0) {
            // remove rank-specific k-v files
            char rank_kvfile[UNIFYFS_MAX_FILENAME];
            DIR* rkv = opendir(sharedfs_rank_kvdir);
            if (NULL == rkv) {
                LOGERR("failed to opendir(%s)", sharedfs_rank_kvdir);
                return (int)UNIFYFS_ERROR_KEYVAL;
            }
            while (NULL != (de = readdir(rkv))) {
                if ((0 == strcmp(".", de->d_name)) ||
                    (0 == strcmp("..", de->d_name))) {
                    continue;
                }
                memset(rank_kvfile, 0, sizeof(rank_kvfile));
                scnprintf(rank_kvfile, sizeof(rank_kvfile), "%s/%s",
                        sharedfs_rank_kvdir, de->d_name);
                rc = remove(rank_kvfile);
                if (rc != 0) {
                    LOGERR("failed to remove rank-specific kvstore entry %s",
                        de->d_name);
                }
            }
            closedir(rkv);

            // remove rank-specific subdir
            rc = rmdir(sharedfs_rank_kvdir);
            if (rc != 0) {
                LOGERR("failed to remove rank-specific kvstore dir %s",
                    sharedfs_rank_kvdir);
                return (int)UNIFYFS_ERROR_KEYVAL;
            }
        }

        // cleanup sharedfs k-v hierarchy root
        if (0 == kv_myrank) {
            memset(&s, 0, sizeof(struct stat));
            rc = stat(sharedfs_kvdir, &s);
            if (rc == 0) {
                int entry_count = 0;
                DIR* skv = opendir(sharedfs_kvdir);
                if (NULL == skv) {
                    LOGERR("failed to opendir(%s)", sharedfs_kvdir);
                    return (int)UNIFYFS_ERROR_KEYVAL;
                }
                while (NULL != (de = readdir(skv))) {
                    if ((0 == strcmp(".", de->d_name)) ||
                        (0 == strcmp("..", de->d_name))) {
                        continue;
                    }
                    entry_count++;
                }
                closedir(skv);
                if (0 == entry_count) {
                    // all subdirs gone, remove root
                    rc = rmdir(sharedfs_kvdir);
                    if (rc != 0) {
                        LOGERR("failed to remove sharedfs kvstore dir %s",
                               sharedfs_kvdir);
                        return (int)UNIFYFS_ERROR_KEYVAL;
                    }
                }
            }
        }
    }
    return (int)UNIFYFS_SUCCESS;
}

// lookup a key-value pair
static int unifyfs_fskv_lookup_local(const char* key,
                                     char** oval)
{
    FILE* kvf;
    char kvfile[UNIFYFS_MAX_FILENAME];
    char kvalue[kv_max_vallen];
    int rc;

    scnprintf(kvfile, sizeof(kvfile), "%s/%s",
             localfs_kvdir, key);
    kvf = fopen(kvfile, "r");
    if (NULL == kvf) {
        LOGERR("failed to open kvstore entry %s", kvfile);
        return (int)UNIFYFS_ERROR_KEYVAL;
    }
    memset(kvalue, 0, sizeof(kvalue));
    rc = fscanf(kvf, "%s\n", kvalue);
    fclose(kvf);
    if (rc != 1) {
        *oval = NULL;
        return (int)UNIFYFS_FAILURE;
    }

    *oval = strdup(kvalue);
    return (int)UNIFYFS_SUCCESS;
}

// publish a key-value pair
static int unifyfs_fskv_publish_local(const char* key,
                                      const char* val)
{
    FILE* kvf;
    char kvfile[UNIFYFS_MAX_FILENAME];

    scnprintf(kvfile, sizeof(kvfile), "%s/%s",
             localfs_kvdir, key);
    kvf = fopen(kvfile, "w");
    if (NULL == kvf) {
        LOGERR("failed to create kvstore entry %s", kvfile);
        return (int)UNIFYFS_ERROR_KEYVAL;
    }
    fprintf(kvf, "%s\n", val);
    fclose(kvf);

    return (int)UNIFYFS_SUCCESS;
}

#if (!defined(USE_PMI2)) && (!defined(USE_PMIX))
static int unifyfs_fskv_lookup_remote(int rank,
                                      const char* key,
                                      char** oval)
{
    FILE* kvf;
    char rank_kvfile[UNIFYFS_MAX_FILENAME];
    char kvalue[kv_max_vallen];
    int rc;

    if (!have_sharedfs_kvstore) {
        return (int)UNIFYFS_ERROR_KEYVAL;
    }

    scnprintf(rank_kvfile, sizeof(rank_kvfile), "%s/%d/%s",
             sharedfs_kvdir, rank, key);
    kvf = fopen(rank_kvfile, "r");
    if (NULL == kvf) {
        LOGERR("failed to open kvstore entry %s", rank_kvfile);
        return (int)UNIFYFS_ERROR_KEYVAL;
    }
    memset(kvalue, 0, sizeof(kvalue));
    rc = fscanf(kvf, "%s\n", kvalue);
    fclose(kvf);

    if (rc != 1) {
        *oval = NULL;
        return (int)UNIFYFS_FAILURE;
    }

    *oval = strdup(kvalue);
    return (int)UNIFYFS_SUCCESS;
}

static int unifyfs_fskv_publish_remote(const char* key,
                                       const char* val)
{
    FILE* kvf;
    char rank_kvfile[UNIFYFS_MAX_FILENAME];

    if (!have_sharedfs_kvstore) {
        return (int)UNIFYFS_ERROR_KEYVAL;
    }

    scnprintf(rank_kvfile, sizeof(rank_kvfile), "%s/%s",
             sharedfs_rank_kvdir, key);
    kvf = fopen(rank_kvfile, "w");
    if (NULL == kvf) {
        LOGERR("failed to create kvstore entry %s", rank_kvfile);
        return (int)UNIFYFS_ERROR_KEYVAL;
    }
    fprintf(kvf, "%s\n", val);
    fclose(kvf);

    return (int)UNIFYFS_SUCCESS;
}

static int unifyfs_fskv_fence(void)
{
    if (!have_sharedfs_kvstore) {
        return (int)UNIFYFS_ERROR_KEYVAL;
    }

    if (1 == kv_nranks) {
        return (int)UNIFYFS_SUCCESS;
    }

    // TODO - use a file as a counting semaphore??
    sleep(10);

    return (int)UNIFYFS_SUCCESS;
}
#endif

//--------------------- K-V Store API ---------------------

// Initialize key-value store
int unifyfs_keyval_init(unifyfs_cfg_t* cfg,
                        int* rank,
                        int* nranks)
{
    int rc;

    if (!kv_initialized) {
        gethostname(kv_myhost, sizeof(kv_myhost));

#if defined(USE_PMIX)
        rc = unifyfs_pmix_init();
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
#elif defined(USE_PMI2)
        rc = unifyfs_pmi2_init();
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
#else
        // use passed-in rank, nranks
        if (NULL != rank) {
            kv_myrank = *rank;
        }
        if (NULL != nranks) {
            kv_nranks = *nranks;
        }
#endif

        // NOTE: do this after getting rank/n_ranks info
        rc = unifyfs_fskv_init(cfg);
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }

        kv_initialized = 1;
    }

    if (NULL != rank) {
        *rank = kv_myrank;
    }
    if (NULL != nranks) {
        *nranks = kv_nranks;
    }

    return (int)UNIFYFS_SUCCESS;
}

// Finalize key-value store
int unifyfs_keyval_fini(void)
{
    int rc;
    if (kv_initialized) {
        rc = unifyfs_fskv_fini();
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }

#if defined(USE_PMIX)
        rc = unifyfs_pmix_fini();
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
#elif defined(USE_PMI2)
        rc = unifyfs_pmi2_fini();
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
#endif

        kv_initialized = 0;
        kv_published = 0;
        kv_max_keylen = 0;
        kv_max_vallen = 0;
    }
    return (int)UNIFYFS_SUCCESS;
}

// Lookup a local key-value pair
int unifyfs_keyval_lookup_local(const char* key,
                                char** oval)
{
    int rc;

    if ((NULL == key) || (NULL == oval)) {
        LOGERR("NULL parameter");
        return EINVAL;
    }

    if (!kv_initialized) {
        rc = unifyfs_keyval_init(NULL, NULL, NULL);
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
    }

    size_t len = strlen(key);
    if (len >= (kv_max_keylen - 1)) {
        LOGERR("length of key (%zd) exceeds max %zd",
               len, kv_max_keylen);
        return EINVAL;
    }

    // do the lookup
    rc = unifyfs_fskv_lookup_local(key, oval);
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("local keyval lookup for '%s' failed", key);
    }
    return rc;
}

// Lookup a remote key-value pair
int unifyfs_keyval_lookup_remote(int rank,
                                 const char* key,
                                 char** oval)
{
    int rc;

    if ((NULL == key) || (NULL == oval)) {
        LOGERR("NULL parameter");
        return EINVAL;
    }

    if (!kv_initialized) {
        rc = unifyfs_keyval_init(NULL, NULL, NULL);
        if (rc != (int)UNIFYFS_SUCCESS) {
            return rc;
        }
    }

    // NOTE: assumes rank value fits in 10 characters
    size_t len = 10 + strlen(key) + 1;
    if (len >= (kv_max_keylen - 1)) {
        LOGERR("length of key (%zd) exceeds max %zd",
               len, kv_max_keylen);
        return EINVAL;
    }

    // generate full key, which includes remote host
    char rank_key[kv_max_keylen];
    memset(rank_key, 0, sizeof(rank_key));
    snprintf(rank_key, sizeof(rank_key), "%d.%s", rank, key);

    // do the lookup
#if defined(USE_PMIX)
    rc = unifyfs_pmix_lookup(rank_key, oval);
#elif defined(USE_PMI2)
    rc = unifyfs_pmi2_lookup(rank_key, oval);
#else
    rc = unifyfs_fskv_lookup_remote(rank, key, oval);
#endif
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("remote keyval lookup for '%s' failed", key);
    }
    return rc;
}

// Publish a locally-visible key-value pair
int unifyfs_keyval_publish_local(const char* key,
                                 const char* val)
{
    int rc;

    if (!kv_initialized) {
        return (int)UNIFYFS_ERROR_KEYVAL;
    }

    if ((key == NULL) || (val == NULL)) {
        LOGERR("NULL key or value");
        return EINVAL;
    }

    size_t len = strlen(key);
    if (len >= (kv_max_keylen - 1)) {
        LOGERR("length of key (%zd) exceeds max %zd",
               len, kv_max_keylen);
        return EINVAL;
    }

    len = strlen(val);
    if (len >= kv_max_vallen) {
        LOGERR("length of val (%zd) exceeds max %zd",
               len, kv_max_vallen);
        return EINVAL;
    }

    // publish it
    rc = unifyfs_fskv_publish_local(key, val);
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("local keyval publish for '%s' failed", key);
    } else {
        kv_published = 1;
    }
    return rc;
}

// Publish a remotely-visible key-value pair
int unifyfs_keyval_publish_remote(const char* key,
                                  const char* val)
{
    int rc;

    if (!kv_initialized) {
        return (int)UNIFYFS_ERROR_KEYVAL;
    }

    if ((key == NULL) || (val == NULL)) {
        LOGERR("NULL key or value");
        return EINVAL;
    }

    // NOTE: assumes rank value fits in 10 characters
    size_t len = 10 + strlen(key) + 1;
    if (len >= (kv_max_keylen - 1)) {
        LOGERR("length of key (%zd) exceeds max %zd",
               len, kv_max_keylen);
        return EINVAL;
    }

    // generate full key, which includes remote host
    char rank_key[kv_max_keylen];
    memset(rank_key, 0, sizeof(rank_key));
    snprintf(rank_key, sizeof(rank_key), "%d.%s", kv_myrank, key);

    // publish it
#if defined(USE_PMIX)
    rc = unifyfs_pmix_publish(rank_key, val);
#elif defined(USE_PMI2)
    rc = unifyfs_pmi2_publish(rank_key, val);
#else
    rc = unifyfs_fskv_publish_remote(key, val);
#endif
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("remote keyval publish for '%s' failed", key);
    } else {
        kv_published = 1;
    }
    return rc;
}

// block until a particular key-value pair published by all servers
int unifyfs_keyval_fence_remote(void)
{
    int rc;
#if defined(USE_PMIX)
    rc = unifyfs_pmix_fence();
#elif defined(USE_PMI2)
    rc = unifyfs_pmi2_fence();
#else
    rc = unifyfs_fskv_fence();
#endif
    return rc;
}
