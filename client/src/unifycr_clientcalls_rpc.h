#ifndef __UNIFYCR_CLIENTCALS_RPC_H
#define __UNIFYCR_CLIENTCALLS_RPC_H

/*******************************************************************************
 * unifycr_clientcalls_rpc.h
 * Declarations for the RPC shared-memory interfaces to the UCR server.
 ********************************************************************************/

#include <unistd.h>
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>


    MERCURY_GEN_PROC(unifycr_read_out_t, ((int32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_read_in_t,
        ((uint64_t)(fid))\
        ((uint64_t)(offset))\
        ((uint32_t)(bulk_size))\
        ((hg_bulk_t)(bulk_handle)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_read_rpc)

#endif
