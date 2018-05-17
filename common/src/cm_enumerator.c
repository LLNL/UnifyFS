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

#include "cm_enumerator.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* c-strings for enum names */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYCR_CM_ENUM_ ## name ## _NAME_STR = #name;
UNIFYCR_CM_ENUMERATOR
#undef ENUMITEM

const char *unifycr_cm_enum_str(unifycr_cm_e e)
{
    switch (e) {
    case UNIFYCR_CM_INVALID:
        return "UNIFYCR_CM_INVALID";
#define ENUMITEM(name, desc)                                    \
    case UNIFYCR_CM_ ## name:                                   \
        return UNIFYCR_CM_ENUM_ ## name ## _NAME_STR;
    UNIFYCR_CM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYCR_CM_ENUM_ ## name ## _DESC_STR = #desc;
UNIFYCR_CM_ENUMERATOR
#undef ENUMITEM

const char *unifycr_cm_enum_description(unifycr_cm_e e)
{
    switch (e) {
    case UNIFYCR_CM_INVALID:
        return "invalid unifycr_cm_e value";
#define ENUMITEM(name, desc)                                    \
    case UNIFYCR_CM_ ## name:                                   \
        return UNIFYCR_CM_ENUM_ ## name ## _DESC_STR;
    UNIFYCR_CM_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifycr_cm_e unifycr_cm_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                    \
    else if (strcmp(s, #name) == 0)             \
        return UNIFYCR_CM_ ## name;
    UNIFYCR_CM_ENUMERATOR;
#undef ENUMITEM

    return UNIFYCR_CM_INVALID;
}

/* validity check */

int check_valid_unifycr_cm_enum(unifycr_cm_e e)
{
    return ((e > UNIFYCR_CM_INVALID) &&
            (e < UNIFYCR_CM_ENUM_MAX) &&
            (unifycr_cm_enum_str(e) != NULL));
}
