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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Enumerator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#include "err_enumerator.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* c-strings for enum names */

#define ENUMITEM(name, desc)                                            \
    const char *UNIFYFS_ERROR_ ## name ## _NAME_STR = #name;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_error_enum_str(unifyfs_error_e e)
{
    switch (e) {
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

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYFS_ERROR_ ## name ## _DESC_STR = #desc;
UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_error_enum_description(unifyfs_error_e e)
{
    switch (e) {
    case UNIFYFS_FAILURE:
        return "Failure";
    case UNIFYFS_SUCCESS:
        return "Success";
#define ENUMITEM(name, desc)                              \
    case UNIFYFS_ERROR_ ## name:                          \
        return UNIFYFS_ERROR_ ## name ## _DESC_STR;
    UNIFYFS_ERROR_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifyfs_error_e unifyfs_error_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                  \
    else if (strcmp(s, #name) == 0)           \
        return UNIFYFS_ERROR_ ## name;
    UNIFYFS_ERROR_ENUMERATOR;
#undef ENUMITEM

    return UNIFYFS_INVALID_ERROR;
}

/* validity check */

int check_valid_unifyfs_error_enum(unifyfs_error_e e)
{
    return ((e > UNIFYFS_INVALID_ERROR) &&
            (e < UNIFYFS_ERROR_MAX) &&
            (unifyfs_error_enum_str(e) != NULL));
}

