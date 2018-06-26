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
    const char *UNIFYCR_ERROR_ ## name ## _NAME_STR = #name;
UNIFYCR_ERROR_ENUMERATOR
#undef ENUMITEM

const char *unifycr_error_enum_str(unifycr_error_e e)
{
    switch (e) {
    case UNIFYCR_FAILURE:
        return "UNIFYCR_FAILURE";
    case UNIFYCR_SUCCESS:
        return "UNIFYCR_SUCCESS";
#define ENUMITEM(name, desc)                              \
    case UNIFYCR_ERROR_ ## name:                          \
        return UNIFYCR_ERROR_ ## name ## _NAME_STR;
    UNIFYCR_ERROR_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

/* c-strings for enum descriptions */

#define ENUMITEM(name, desc)                                    \
    const char *UNIFYCR_ERROR_ ## name ## _DESC_STR = #desc;
UNIFYCR_ERROR_ENUMERATOR
#undef ENUMITEM

const char *unifycr_error_enum_description(unifycr_error_e e)
{
    switch (e) {
    case UNIFYCR_FAILURE:
        return "Failure";
    case UNIFYCR_SUCCESS:
        return "Success";
#define ENUMITEM(name, desc)                              \
    case UNIFYCR_ERROR_ ## name:                          \
        return UNIFYCR_ERROR_ ## name ## _DESC_STR;
    UNIFYCR_ERROR_ENUMERATOR
#undef ENUMITEM
    default :
        break;
    }
    return NULL;
}

unifycr_error_e unifycr_error_enum_from_str(const char *s)
{
    if (0)
        ;
#define ENUMITEM(name, desc)                  \
    else if (strcmp(s, #name) == 0)           \
        return UNIFYCR_ERROR_ ## name;
    UNIFYCR_ERROR_ENUMERATOR;
#undef ENUMITEM

    return UNIFYCR_INVALID_ERROR;
}

/* validity check */

int check_valid_unifycr_error_enum(unifycr_error_e e)
{
    return ((e > UNIFYCR_INVALID_ERROR) &&
            (e < UNIFYCR_ERROR_MAX) &&
            (unifycr_error_enum_str(e) != NULL));
}

