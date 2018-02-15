/*******************************************************************************************
 * unifycr_clientcalls_rpc.cc - Implements the RPC handlers for the
 * application-client, shared-memory interface.
 ********************************************************************************************/

#include <cstdio>
#include <cassert>

//#include "unifycr_global_ops.h"
#include "unifycr_clientcalls_rpc.h"

/* unifycr_read_rpc - Transfers a dummy buffer back to the client following a (valid) address look-up.
 * Should be updated to call the read transfer function in the interserver_client component. */
static void unifycr_read_rpc(hg_handle_t handle)
{
    //implement read rpc here
}
DEFINE_MARGO_RPC_HANDLER(unifycr_read_rpc)

