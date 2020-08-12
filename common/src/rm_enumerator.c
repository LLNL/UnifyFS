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

#include "rm_enumerator.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* c-strings for enum names */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYFS_RM_ENUM_ ## name ## _NAME_STR = #name;
UNIFYFS_RM_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_rm_enum_str(unifyfs_rm_e e)
{
    switch (e) {
    case UNIFYFS_RM_INVALID:
        return "UNIFYFS_RM_INVALID";
#define ENUMITEM(name, desc)                                    \
    case UNIFYFS_RM_ ## name:                                   \
        return UNIFYFS_RM_ENUM_ ## name ## _NAME_STR;
    UNIFYFS_RM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYFS_RM_ENUM_ ## name ## _DESC_STR = #desc;
UNIFYFS_RM_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_rm_enum_description(unifyfs_rm_e e)
{
    switch (e) {
    case UNIFYFS_RM_INVALID:
        return "invalid unifyfs_rm_e value";
#define ENUMITEM(name, desc)                                    \
    case UNIFYFS_RM_ ## name:                                   \
        return UNIFYFS_RM_ENUM_ ## name ## _DESC_STR;
    UNIFYFS_RM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifyfs_rm_e unifyfs_rm_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                    \
    else if (strcmp(s, #name) == 0)             \
        return UNIFYFS_RM_ ## name;
    UNIFYFS_RM_ENUMERATOR;
#undef ENUMITEM

    return UNIFYFS_RM_INVALID;
}

/* validity check */

int check_valid_unifyfs_rm_enum(unifyfs_rm_e e)
{
    return ((e > UNIFYFS_RM_INVALID) &&
            (e < UNIFYFS_RM_ENUM_MAX) &&
            (unifyfs_rm_enum_str(e) != NULL));
}
