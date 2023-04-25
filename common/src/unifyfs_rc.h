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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Enumerator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#ifndef _UNIFYFS_RC_ENUMERATOR_H_
#define _UNIFYFS_RC_ENUMERATOR_H_

#include <errno.h>

/* #define __ELASTERROR if our errno.h doesn't define it for us */
#ifndef __ELASTERROR
#define __ELASTERROR 1000
#endif

/* NOTE: If POSIX errno.h defines an error code that we can use sensibly,
 * don't create a duplicate one for UnifyFS */

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYFS_ERROR_ENUMERATOR                                       \
    ENUMITEM(BADCONFIG, "Configuration has invalid setting")           \
    ENUMITEM(GOTCHA, "Gotcha operation error")                         \
    ENUMITEM(KEYVAL, "Key-value store operation error")                \
    ENUMITEM(MARGO, "Mercury/Argobots operation error")                \
    ENUMITEM(NYI, "Not yet implemented")                               \
    ENUMITEM(PMI, "PMI2/PMIx error")                                   \
    ENUMITEM(SHMEM, "Shared memory region init/access error")          \
    ENUMITEM(THREAD, "POSIX thread operation failed")                  \
    ENUMITEM(TIMEOUT, "Timed out")                                     \


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief enum for UnifyFS return codes
 */
typedef enum {
    UNIFYFS_INVALID_RC = -2,
    UNIFYFS_FAILURE = -1,
    UNIFYFS_SUCCESS = 0,
    /* Start our error numbers after the standard errno.h ones */
    UNIFYFS_BEGIN_ERRORS = __ELASTERROR,
#define ENUMITEM(name, desc) \
    UNIFYFS_ERROR_ ## name,
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM
    UNIFYFS_END_ERRORS
} unifyfs_rc;

/**
 * @brief get C-string for given error enum value
 */
const char* unifyfs_rc_enum_str(unifyfs_rc rc);

/**
 * @brief get description for given error enum value
 */
const char* unifyfs_rc_enum_description(unifyfs_rc rc);

/**
 * @brief check validity of given error enum value
 */
int check_valid_unifyfs_rc_enum(unifyfs_rc rc);

/**
 * @brief get enum value for given error C-string
 */
unifyfs_rc unifyfs_rc_enum_from_str(const char* s);

/**
 * @brief convert a UnifyFS error to an errno value
 */
int unifyfs_rc_errno(unifyfs_rc rc);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYFS_RC_ENUMERATOR_H */
