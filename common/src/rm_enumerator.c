/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
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
    const char *UNIFYCR_RM_ENUM_ ## name ## _NAME_STR = #name;
UNIFYCR_RM_ENUMERATOR
#undef ENUMITEM

const char *unifycr_rm_enum_str(unifycr_rm_e e)
{
    switch (e) {
    case UNIFYCR_RM_INVALID:
        return "UNIFYCR_RM_INVALID";
#define ENUMITEM(name, desc)                                    \
    case UNIFYCR_RM_ ## name:                                   \
        return UNIFYCR_RM_ENUM_ ## name ## _NAME_STR;
    UNIFYCR_RM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYCR_RM_ENUM_ ## name ## _DESC_STR = #desc;
UNIFYCR_RM_ENUMERATOR
#undef ENUMITEM

const char *unifycr_rm_enum_description(unifycr_rm_e e)
{
    switch (e) {
    case UNIFYCR_RM_INVALID:
        return "invalid unifycr_rm_e value";
#define ENUMITEM(name, desc)                                    \
    case UNIFYCR_RM_ ## name:                                   \
        return UNIFYCR_RM_ENUM_ ## name ## _DESC_STR;
    UNIFYCR_RM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifycr_rm_e unifycr_rm_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                    \
    else if (strcmp(s, #name) == 0)             \
        return UNIFYCR_RM_ ## name;
    UNIFYCR_RM_ENUMERATOR;
#undef ENUMITEM

    return UNIFYCR_RM_INVALID;
}

/* validity check */

int check_valid_unifycr_rm_enum(unifycr_rm_e e)
{
    return ((e > UNIFYCR_RM_INVALID) &&
            (e < UNIFYCR_RM_ENUM_MAX) &&
            (unifycr_rm_enum_str(e) != NULL));
}
