==============================
UnifyFS API for I/O Middleware
==============================

This section describes the purpose, concepts, and usage of the UnifyFS
library API.

-------------------
Library API Purpose
-------------------

The UnifyFS library API provides a direct interface for UnifyFS configuration,
namespace management, and batched file I/O and transfer operations. The library
is primarily targeted for use by I/O middleware software such as HDF5 and
VeloC, but is also useful for user applications needing programmatic control
and interactions with UnifyFS.

.. note::
    Use of the library API is *not* required for most applications, as UnifyFS
    will transparently intercept I/O operations made by the application. See
    :doc:`examples` for examples of typical application usage.

--------------------
Library API Concepts
--------------------

Namespace (aka Mountpoint)
**************************

All UnifyFS clients provide the mountpoint prefix (e.g., "/unifyfs") that is
used to distinguish the UnifyFS namespace from other file systems available
to the client application. All absolute file paths that include the mountpoint
prefix are treated as belonging to the associated UnifyFS namespace.

Using the library API, an application or I/O middleware system can operate on
multiple UnifyFS namespaces concurrently.

File System Handle
******************

All library API methods require a file system handle parameter of type
``unifyfs_handle``. Users obtain a valid handle via an API call to
``unifyfs_initialize()``, which specifies the mountpoint prefix and
configuration settings associated with the handle.

Multiple handles can be acquired by the same client. This permits access to
multiple namespaces, or different configured behaviors for the same namespace.

Global File Identifier
**********************

A global file identifier (gfid) is a unique integer identifier for a given
absolute file path within a UnifyFS namespace. Clients accessing the exact
same file path are guaranteed to obtain the same gfid value when creating or
opening the file. I/O operations use the gfid to identify the target file.

Note that unlike POSIX file descriptors, a gfid is strictly a unique identifier
and has no associated file state such as a current file position pointer. As
such, it is valid to obtain the gfid for a file in a single process (e.g., via
file creation), and then share the resulting gfid value among other parallel
processes via a collective communication mechanism.

-----------------
Library API Types
-----------------

The file system handle type is a pointer to an opaque client structure that
records the associated mountpoint and configuration.

.. code-block:: c
    :caption: File system handle type

    /* UnifyFS file system handle (opaque pointer) */
    typedef struct unifyfs_client* unifyfs_handle;

I/O requests take the form of a ``unifyfs_io_request`` structure that includes
the target file gfid, the specific I/O operation (``unifyfs_ioreq_op``) to be
applied, and associated operation parameters such as the file offset or user
buffer and size. The structure also contains fields used for tracking the
status of the request (``unifyfs_req_state``) and operation results
(``unifyfs_ioreq_result``).

.. code-block:: c
    :caption: File I/O request types

    /* I/O request structure */
    typedef struct unifyfs_io_request {
        /* user-specified fields */
        void* user_buf;
        size_t nbytes;
        off_t offset;
        unifyfs_gfid gfid;
        unifyfs_ioreq_op op;

        /* status/result fields */
        unifyfs_req_state state;
        unifyfs_ioreq_result result;
    } unifyfs_io_request;

    /* Enumeration of supported I/O request operations */
    typedef enum unifyfs_ioreq_op {
        UNIFYFS_IOREQ_NOP = 0,
        UNIFYFS_IOREQ_OP_READ,
        UNIFYFS_IOREQ_OP_WRITE,
        UNIFYFS_IOREQ_OP_SYNC_DATA,
        UNIFYFS_IOREQ_OP_SYNC_META,
        UNIFYFS_IOREQ_OP_TRUNC,
        UNIFYFS_IOREQ_OP_ZERO,
    } unifyfs_ioreq_op;

    /* Enumeration of API request states */
    typedef enum unifyfs_req_state {
        UNIFYFS_REQ_STATE_INVALID = 0,
        UNIFYFS_REQ_STATE_IN_PROGRESS,
        UNIFYFS_REQ_STATE_CANCELED,
        UNIFYFS_REQ_STATE_COMPLETED
    } unifyfs_req_state;

    /* Structure containing I/O request result values */
    typedef struct unifyfs_ioreq_result {
        int error;
        int rc;
        size_t count;
    } unifyfs_ioreq_result;

For the ``unifyfs_ioreq_result`` structure, successful operations will set the
``rc`` and ``count`` fields as applicable to the specific operation type. All
operational failures are reported by setting the ``error`` field to a non-zero
value corresponding the the operation failure code, which is often a POSIX
``errno`` value.

File transfer requests use a ``unifyfs_transfer_request`` structure that
includes the source and destination file paths, transfer mode, and a flag
indicating whether parallel file transfer should be used. Similar to I/O
requests, the structure also contains fields used for tracking the request
status and transfer operation result.

.. code-block:: c
    :caption: File transfer request types

    /* File transfer request structure */
    typedef struct unifyfs_transfer_request {
        /* user-specified fields */
        const char* src_path;
        const char* dst_path;
        unifyfs_transfer_mode mode;
        int use_parallel;

        /* status/result fields */
        unifyfs_req_state state;
        unifyfs_transfer_result result;
    } unifyfs_transfer_request;

    /* Enumeration of supported I/O request operations */
    typedef enum unifyfs_transfer_mode {
        UNIFYFS_TRANSFER_MODE_INVALID = 0,
        UNIFYFS_TRANSFER_MODE_COPY, // simple copy to destination
        UNIFYFS_TRANSFER_MODE_MOVE  // copy, then remove source
    } unifyfs_transfer_mode;

    /* File transfer result structure */
    typedef struct unifyfs_transfer_result {
        int error;
        int rc;
        size_t file_size_bytes;
        double transfer_time_seconds;
    } unifyfs_transfer_result;

-------------------------
Example Library API Usage
-------------------------

To get started using the library API, please add the following to your client
source code files that will make calls to API methods. You will also need to
modify your client application build process to link with the
``libunifyfs_api`` library.

.. code-block:: c
    :caption: Including the API header

    #include <unifyfs/unifyfs_api.h>

The common pattern for using the library API is to initialize a UnifyFS file
system handle, perform a number of operations using that handle, and then
release the handle. As previously mentioned, the same client process may
initialize multiple file system handles and use them concurrently, either
to work with multiple namespaces, or to use different configured behaviors
with different handles sharing the same namespace.

File System Handle Initialization and Finalization
**************************************************

To initialize a handle to UnifyFS, the client application uses the
``unifyfs_initialize()`` method as shown below. This method takes the namespace
mountpoint prefix and an array of optional configuration parameter settings as
input parameters, and initializes the value of the passed file system handle
upon success.

In the example below, the ``logio.chunk_size`` configuration
parameter, which controls the size of the log-based I/O data chunks, is set to
the value of 32768. See :doc:`configuration`
for further options for customizing the behavior of UnifyFS.

.. code-block:: c
    :caption: UnifyFS handle initialization

    int n_configs = 1;
    unifyfs_cfg_option chk_size = { .opt_name = "logio.chunk_size",
                                    .opt_value = "32768" };

    const char* unifyfs_prefix = "/my/unifyfs/namespace";
    unifyfs_handle fshdl = UNIFYFS_INVALID_HANDLE;
    int rc = unifyfs_initialize(unifyfs_prefix, &chk_size, n_configs, &fshdl);

Once all UnifyFS operation using the handle have been completed, the client
application should call ``unifyfs_finalize()`` as shown below to release the
resources associated with the handle.

.. code-block:: c
    :caption: UnifyFS handle finalization

    int rc = unifyfs_finalize(fshdl);

File Creation, Use, and Removal
*******************************

New files should be created by a single client process using ``unifyfs_create()``
as shown below. Note that if multiple clients attempt to create the same file,
only one will succeed.

.. note::
    Currently, the ``create_flags`` parameter is unused; it
    is reserved for future use to indicate file-specific UnifyFS behavior.

.. code-block:: c
    :caption: UnifyFS file creation

    const char* filename = "/my/unifyfs/namespace/a/new/file";
    int create_flags = 0;
    unifyfs_gfid gfid = UNIFYFS_INVALID_GFID;
    int rc = unifyfs_create(fshdl, create_flags, filename, &gfid);

Existing files can be opened by any client process using ``unifyfs_open()``.

.. code-block:: c
    :caption: UnifyFS file use

    const char* filename = "/my/unifyfs/namespace/an/existing/file";
    unifyfs_gfid gfid = UNIFYFS_INVALID_GFID;
    int access_flags = O_RDWR;
    int rc = unifyfs_open(fshdl, access_flags, filename, &gfid);

When no longer required, files can be deleted using ``unifyfs_remove()``.

.. code-block:: c
    :caption: UnifyFS file removal

    const char* filename = "/my/unifyfs/namespace/an/existing/file";
    int rc = unifyfs_remove(fshdl, filename);

Batched File I/O
****************

File I/O operations in the library API use a batched request interface similar
to POSIX ``lio_listio()``. A client application dispatches an array of I/O
operation requests, where each request identifies the target file gfid, the
operation type (e.g., read, write, or truncate), and associated operation
parameters. Upon successful dispatch, the operations will be executed by
UnifyFS in an asynchronous manner that allows the client to overlap other
computation with I/O. The client application must then explicitly wait for
completion of the requests in the batch. After an individual request has been
completed (or canceled by the client), the request's operation results
can be queried.

When dispatching a set of requests that target the same file, there is an order
imposed on the types of operations. First, all read operations are processed,
followed by writes, then truncations, and finally synchronization operations.
Note that this means a read request will not observe any data written in the
same batch.

A simple use case for batched I/O is shown below, where the client dispatches
a batch of requests including several rank-strided write operations followed by
a metadata sync to make those writes visible to other clients, and then
immediately waits for completion of the entire batch.

.. code-block:: c
    :caption: Synchronous Batched I/O

    /* write and sync file metadata */
    size_t n_chks = 10;
    size_t chunk_size = 1048576;
    size_t block_size = chunk_size * total_ranks;
    size_t n_reqs = n_chks + 1;
    unifyfs_io_request my_reqs[n_reqs];
    for (size_t i = 0; i < n_chks; i++) {
        my_reqs[i].op = UNIFYFS_IOREQ_OP_WRITE;
        my_reqs[i].gfid = gfid;
        my_reqs[i].nbytes = chunk_size;
        my_reqs[i].offset = (off_t)((i * block_size) + (my_rank * chunk_size));
        my_reqs[i].user_buf = my_databuf + (i * chksize);
    }
    my_reqs[n_chks].op = UNIFYFS_IOREQ_OP_SYNC_META;
    my_reqs[n_chks].gfid = gfid;

    rc = unifyfs_dispatch_io(fshdl, n_reqs, my_reqs);
    if (rc == UNIFYFS_SUCCESS) {
        int waitall = 1;
        rc = unifyfs_wait_io(fshdl, n_reqs, my_reqs, waitall);
        if (rc == UNIFYFS_SUCCESS) {
            for (size_t i = 0; i < n_reqs; i++) {
                assert(my_reqs[i].result.error == 0);
            }
        }
    }

Batched File Transfers
**********************

File transfer operations in the library API also use a batched request
interface. A client application dispatches an array of file transfer
requests, where each request identifies the source and destination file
paths and the transfer mode. Two transfer modes are currently supported:

1. COPY - Copy source file to destination path.
2. MOVE - Copy source file to destination path, then remove source file.

Upon successful dispatch, the transfer operations will be executed by
UnifyFS in an asynchronous manner that allows the client to overlap other
computation with I/O. The client application must then explicitly wait for
completion of the requests in the batch. After an individual request has been
completed (or canceled by the client), the request's operation results
can be queried.

A simple use case for batched transfer is shown below, where the client
dispatches a batch of requests and then immediately waits for completion of
the entire batch.

.. code-block:: c
    :caption: Synchronous Batched File Transfers

    /* move output files from UnifyFS to parallel file system */
    const char* destfs_prefix = "/some/parallel/filesystem/location";
    size_t n_files = 3;
    unifyfs_transfer_request my_reqs[n_files];
    char src_file[PATHLEN_MAX];
    char dst_file[PATHLEN_MAX];
    for (int i = 0; i < (int)n_files; i++) {
        snprintf(src_file, sizeof(src_file), "%s/file.%d", unifyfs_prefix, i);
        snprintf(dst_file, sizeof(src_file), "%s/file.%d", destfs_prefix, i);
        my_reqs[i].src_path = strdup(src_file);
        my_reqs[i].dst_path = strdup(dst_file);
        my_reqs[i].mode = UNIFYFS_TRANSFER_MODE_MOVE;
        my_reqs[i].use_parallel = 1;
    }

    rc = unifyfs_dispatch_transfer(fshdl, n_files, my_reqs);
    if (rc == UNIFYFS_SUCCESS) {
        int waitall = 1;
        rc = unifyfs_wait_transfer(fshdl, n_files, my_reqs, waitall);
        if (rc == UNIFYFS_SUCCESS) {
            for (int i = 0; i < (int)n_files; i++) {
                assert(my_reqs[i].result.error == 0);
            }
        }
    }

More Examples
*************

Additional examples demonstrating use of the library API can be found in
the unit tests (see api-unit-tests_).

.. explicit external hyperlink targets

.. _api-unit-tests: https://github.com/LLNL/UnifyFS/blob/dev/t/api