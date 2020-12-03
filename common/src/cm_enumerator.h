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

#ifndef _UNIFYFS_CM_ENUMERATOR_H_
#define _UNIFYFS_CM_ENUMERATOR_H_

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYFS_CM_ENUMERATOR                                   \
    ENUMITEM(NONE, "no consistency")                            \
    ENUMITEM(LAMINATED, "UnifyFS laminated consistency model")  \
    ENUMITEM(POSIX, "POSIX I/O consistency model")

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief supported consistency models
 */
typedef enum {
    UNIFYFS_CM_INVALID = 0,
#define ENUMITEM(name, desc)                     \
        UNIFYFS_CM_ ## name,
    UNIFYFS_CM_ENUMERATOR
#undef ENUMITEM
    UNIFYFS_CM_ENUM_MAX
} unifyfs_cm_e;

/**
 * @brief get C-string for given consistency model enum value
 */
const char *unifyfs_cm_enum_str(unifyfs_cm_e e);

/**
 * @brief get description for given consistency model enum value
 */
const char *unifyfs_cm_enum_description(unifyfs_cm_e e);

/**
 * @brief check validity of given consistency model enum value
 */
int check_valid_unifyfs_cm_enum(unifyfs_cm_e e);

/**
 * @brief get enum value for given consistency model C-string
 */
unifyfs_cm_e unifyfs_cm_enum_from_str(const char *s);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYFS_CM_ENUMERATOR_H */
