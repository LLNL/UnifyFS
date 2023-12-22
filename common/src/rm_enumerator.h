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

#ifndef _UNIFYFS_RM_ENUMERATOR_H_
#define _UNIFYFS_RM_ENUMERATOR_H_

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYFS_RM_ENUMERATOR                                           \
    ENUMITEM(PBS, "Portable Batch System / TORQUE")                     \
    ENUMITEM(SLURM, "SchedMD SLURM")                                    \
    ENUMITEM(LSF, "IBM Spectrum LSF")                                   \
    ENUMITEM(LSF_CSM, "IBM Spectrum LSF with Cluster System Management") \
    ENUMITEM(FLUX, "Flux")                                              \

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief supported resource managers
 */
typedef enum {
    UNIFYFS_RM_INVALID = 0,
#define ENUMITEM(name, desc)                    \
        UNIFYFS_RM_ ## name,
    UNIFYFS_RM_ENUMERATOR
#undef ENUMITEM
    UNIFYFS_RM_ENUM_MAX
} unifyfs_rm_e;

/**
 * @brief get resource manager C-string for given enum value
 */
const char *unifyfs_rm_enum_str(unifyfs_rm_e e);

/**
 * @brief get resource manager description for given enum value
 */
const char *unifyfs_rm_enum_description(unifyfs_rm_e e);

/**
 * @brief check validity of given resource manager enum value
 */
int check_valid_unifyfs_rm_enum(unifyfs_rm_e e);

/**
 * @brief get resource manager enum value for given C-string
 */
unifyfs_rm_e unifyfs_rm_enum_from_str(const char *s);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYFS_RM_ENUMERATOR_H */
