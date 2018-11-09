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

#include "unifycr_const.h"
#include "unifycr_client_context.h"

#include <string.h>

int unifycr_pack_client_context(unifycr_client_context_t *ctx,
                                char *buffer)
{
    memcpy(buffer, ctx, sizeof(unifycr_client_context_t));
    return UNIFYCR_SUCCESS;
}

int unifycr_unpack_client_context(char *buffer,
                                  unifycr_client_context_t *ctx)
{
    memcpy(ctx, buffer, sizeof(unifycr_client_context_t));
    return UNIFYCR_SUCCESS;
}
