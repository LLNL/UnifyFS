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

#ifndef _UNIFYCR_RM_ENUMERATOR_H_
#define _UNIFYCR_RM_ENUMERATOR_H_

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYCR_RM_ENUMERATOR                                           \
    ENUMITEM(PBS, "Portable Batch System / TORQUE")                     \
    ENUMITEM(SLURM, "SchedMD SLURM")                                    \
    ENUMITEM(LSF, "IBM Spectrum LSF")                                   \
    ENUMITEM(LSF_CSM, "IBM Spectrum LSF with Cluster System Management") \

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief supported resource managers
 */
typedef enum {
    UNIFYCR_RM_INVALID = 0,
#define ENUMITEM(name, desc)                    \
        UNIFYCR_RM_ ## name,
    UNIFYCR_RM_ENUMERATOR
#undef ENUMITEM
    UNIFYCR_RM_ENUM_MAX
} unifycr_rm_e;

/**
 * @brief get resource manager C-string for given enum value
 */
const char *unifycr_rm_enum_str(unifycr_rm_e e);

/**
 * @brief get resource manager description for given enum value
 */
const char *unifycr_rm_enum_description(unifycr_rm_e e);

/**
 * @brief check validity of given resource manager enum value
 */
int check_valid_unifycr_rm_enum(unifycr_rm_e e);

/**
 * @brief get resource manager enum value for given C-string
 */
unifycr_rm_e unifycr_rm_enum_from_str(const char *s);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYCR_RM_ENUMERATOR_H */
