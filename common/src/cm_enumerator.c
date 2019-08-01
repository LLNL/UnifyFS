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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Enumerator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#include "cm_enumerator.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* c-strings for enum names */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYFS_CM_ENUM_ ## name ## _NAME_STR = #name;
UNIFYFS_CM_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_cm_enum_str(unifyfs_cm_e e)
{
    switch (e) {
    case UNIFYFS_CM_INVALID:
        return "UNIFYFS_CM_INVALID";
#define ENUMITEM(name, desc)                                    \
    case UNIFYFS_CM_ ## name:                                   \
        return UNIFYFS_CM_ENUM_ ## name ## _NAME_STR;
    UNIFYFS_CM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYFS_CM_ENUM_ ## name ## _DESC_STR = #desc;
UNIFYFS_CM_ENUMERATOR
#undef ENUMITEM

const char *unifyfs_cm_enum_description(unifyfs_cm_e e)
{
    switch (e) {
    case UNIFYFS_CM_INVALID:
        return "invalid unifyfs_cm_e value";
#define ENUMITEM(name, desc)                                    \
    case UNIFYFS_CM_ ## name:                                   \
        return UNIFYFS_CM_ENUM_ ## name ## _DESC_STR;
    UNIFYFS_CM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifyfs_cm_e unifyfs_cm_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                    \
    else if (strcmp(s, #name) == 0)             \
        return UNIFYFS_CM_ ## name;
    UNIFYFS_CM_ENUMERATOR;
#undef ENUMITEM

    return UNIFYFS_CM_INVALID;
}

/* validity check */

int check_valid_unifyfs_cm_enum(unifyfs_cm_e e)
{
    return ((e > UNIFYFS_CM_INVALID) &&
            (e < UNIFYFS_CM_ENUM_MAX) &&
            (unifyfs_cm_enum_str(e) != NULL));
}
