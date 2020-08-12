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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Enumerator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unifyfs_rc.h"


/* c-strings for enum names */

#define ENUMITEM(name, desc) \
    const char* UNIFYFS_ERROR_ ## name ## _NAME_STR = #name;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM

const char* unifyfs_rc_enum_str(unifyfs_rc rc)
{
    switch (rc) {
    case UNIFYFS_FAILURE:
        return "UNIFYFS_FAILURE";
    case UNIFYFS_SUCCESS:
        return "UNIFYFS_SUCCESS";
#define ENUMITEM(name, desc)                              \
    case UNIFYFS_ERROR_ ## name:                          \
        return UNIFYFS_ERROR_ ## name ## _NAME_STR;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc) \
    const char* UNIFYFS_ERROR_ ## name ## _DESC_STR = #desc;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM

char posix_errstr[1024];

const char* unifyfs_rc_enum_description(unifyfs_rc rc)
{
    switch (rc) {
    case UNIFYFS_FAILURE:
        return "Failure";
    case UNIFYFS_SUCCESS:
        return "Success";
#define ENUMITEM(name, desc)                              \
    case UNIFYFS_ERROR_ ## name:                          \
        return UNIFYFS_ERROR_ ## name ## _DESC_STR;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM
    default:
        /* assume it's a POSIX errno value */
        snprintf(posix_errstr, sizeof(posix_errstr), "%s",
                 strerror((int)rc));
        return (const char*)posix_errstr;
    }
    return NULL;
}

unifyfs_rc unifyfs_rc_enum_from_str(const char* s)
{
    if (strcmp(s, "Success") == 0) {
        return UNIFYFS_SUCCESS;
    } else if (strcmp(s, "Failure") == 0) {
        return UNIFYFS_FAILURE;
    }
#define ENUMITEM(name, desc)                  \
    else if (strcmp(s, #name) == 0) {         \
        return UNIFYFS_ERROR_ ## name;        \
    }
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM

    return UNIFYFS_INVALID_RC;
}

/* validity check */
int check_valid_unifyfs_rc_enum(unifyfs_rc rc)
{
    return ((rc > UNIFYFS_INVALID_RC) &&
            (rc < UNIFYFS_END_ERRORS) &&
            (unifyfs_rc_enum_str(rc) != NULL));
}

/* convert to an errno value */
int unifyfs_rc_errno(unifyfs_rc rc)
{
    if (rc == UNIFYFS_SUCCESS) {
        return 0;
    } else if (rc == UNIFYFS_INVALID_RC) {
        return EINVAL;
    } else if ((rc == UNIFYFS_FAILURE) ||
              ((rc > UNIFYFS_BEGIN_ERRORS) && (rc < UNIFYFS_END_ERRORS))) {
        /* none of our custom errors have good errno counterparts, use EIO */
        return EIO;
    } else {
        /* should be a normal errno value already */
        return (int)rc;
    }
}
