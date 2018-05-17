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

#ifndef _UNIFYCR_CM_ENUMERATOR_H_
#define _UNIFYCR_CM_ENUMERATOR_H_

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYCR_CM_ENUMERATOR                                   \
    ENUMITEM(NONE, "no consistency")                            \
    ENUMITEM(LAMINATED, "UnifyCR laminated consistency model")  \
    ENUMITEM(POSIX, "POSIX I/O consistency model")

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief supported consistency models
 */
typedef enum {
    UNIFYCR_CM_INVALID = 0,
#define ENUMITEM(name, desc)                     \
        UNIFYCR_CM_ ## name,
    UNIFYCR_CM_ENUMERATOR
#undef ENUMITEM
    UNIFYCR_CM_ENUM_MAX
} unifycr_cm_e;

/**
 * @brief get C-string for given consistency model enum value
 */
const char *unifycr_cm_enum_str(unifycr_cm_e e);

/**
 * @brief get description for given consistency model enum value
 */
const char *unifycr_cm_enum_description(unifycr_cm_e e);

/**
 * @brief check validity of given consistency model enum value
 */
int check_valid_unifycr_cm_enum(unifycr_cm_e e);

/**
 * @brief get enum value for given consistency model C-string
 */
unifycr_cm_e unifycr_cm_enum_from_str(const char *s);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYCR_CM_ENUMERATOR_H */
