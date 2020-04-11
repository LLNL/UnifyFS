// server headers
#include "unifyfs_global.h"
#include "unifyfs_tree.h"

#include "abt.h"
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
/**
 * @brief given the process's rank and the number of ranks, this computes a k-ary
 *        tree rooted at rank 0, the structure records the number of children
 *        of the local rank and the list of their ranks
 *
 * @param rank rank of calling process
 * @param ranks number of ranks in tree
 * @param root rank of root of tree
 * @param k degree of k-ary tree
 * @param t output tree structure
 */
void unifyfs_tree_init(
    int rank,          /* rank of calling process */
    int ranks,         /* number of ranks in tree */
    int root,          /* rank of root of tree */
    int k,             /* degree of k-ary tree */
    unifyfs_tree_t* t) /* output tree structure */
{
    int i;

    /* compute distance from our rank to root,
     * rotate ranks to put root as rank 0 */
    rank -= root;
    if (rank < 0) {
        rank += ranks;
    }

    /* compute parent and child ranks with root as rank 0 */

    /* initialize fields */
    t->rank        = rank;
    t->ranks       = ranks;
    t->parent_rank = -1;
    t->child_count = 0;
    t->child_ranks = NULL;

    /* compute the maximum number of children this task may have */
    int max_children = k;

    /* allocate memory to hold list of children ranks */
    if (max_children > 0) {
        size_t bytes = (size_t)max_children * sizeof(int);
        t->child_ranks = (int*) malloc(bytes);

        if (t->child_ranks == NULL) {
            LOGERR("Failed to allocate memory for child rank array");
        }
    }

    /* initialize all ranks to NULL */
    for (i = 0; i < max_children; i++) {
        t->child_ranks[i] = -1;
    }

    /* compute rank of our parent if we have one */
    if (rank > 0) {
        t->parent_rank = (rank - 1) / k;
    }

    /* identify ranks of what would be leftmost
     * and rightmost children */
    int left  = rank * k + 1;
    int right = rank * k + k;

    /* if we have at least one child,
     * compute number of children and list of child ranks */
    if (left < ranks) {
        /* adjust right child in case we don't have a full set of k */
        if (right >= ranks) {
            right = ranks - 1;
        }

        /* compute number of children */
        t->child_count = right - left + 1;

        /* fill in rank for each child */
        for (i = 0; i < t->child_count; i++) {
            t->child_ranks[i] = left + i;
        }
    }

    /* rotate tree neighbor ranks to use global ranks */

    /* rotate our rank in tree */
    t->rank += root;
    if (t->rank >= ranks) {
        t->rank -= ranks;
    }

    /* rotate rank of our parent in tree if we have one */
    if (t->parent_rank != -1) {
        t->parent_rank += root;
        if (t->parent_rank >= ranks) {
            t->parent_rank -= ranks;
        }
    }

    /* rotate rank of each child in tree */
    for (i = 0; i < t->child_count; i++) {
        t->child_ranks[i] += root;
        if (t->child_ranks[i] >= ranks) {
            t->child_ranks[i] -= ranks;
        }
    }

    return;
}

void unifyfs_tree_free(unifyfs_tree_t* t)
{
    /* free child rank list */
    free(t->child_ranks);
    t->child_ranks = NULL;

    return;
}

unifyfs_coll_state_t* unifyfs_coll_state_alloc(
    int root,
    int gfid,
    int32_t ptag,
    int32_t tag)
{
    unifyfs_coll_state_t* st = (unifyfs_coll_state_t*) calloc(1, sizeof(*st));

    st->root          = root;
    st->gfid          = gfid;
    st->parent_tag    = ptag;
    st->tag           = tag;
    st->num_responses = 0;
    st->filesize      = 0;
    st->err           = UNIFYFS_SUCCESS;
    st->mutex         = ABT_MUTEX_NULL;
    st->cond          = ABT_COND_NULL;

    /* define tree to use for given root */
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, root, 2,
        &st->tree);

    return st;
}

void unifyfs_coll_state_free(unifyfs_coll_state_t** pst)
{
    if (pst != NULL) {
        unifyfs_coll_state_t* st = *pst;

        unifyfs_tree_free(&st->tree);

        if (st->cond != ABT_COND_NULL) {
            ABT_cond_free(&st->cond);
        }
        if (st->mutex != ABT_MUTEX_NULL) {
            ABT_mutex_free(&st->mutex);
        }

        free(st);

        *pst = NULL;
    }
}

/* lookup function that maps tag to collective state object,
 * for now we only have the one global structure */
static unifyfs_coll_state_t* get_coll_state(int32_t tag)
{
    // TODO: lookup tag-->state entry in tag2state map, return state
    void* value = int2void_find(&glb_tag2state, tag);
    if (value == NULL) {
        // ERROR, not found
    }
    return (unifyfs_coll_state_t*)value;
}

/******************************************************************
 * filesize 
 ******************************************************************/

static int rpc_invoke_filesize_request(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.filesize_request_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    filesize_request_in_t in;
    in.root = (int32_t)st->root;
    in.tag  = (int32_t)st->tag;
    in.gfid = (int32_t)st->gfid;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    filesize_request_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_filesize_response(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.filesize_response_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    filesize_response_in_t in;
    in.tag      = (int32_t)  st->parent_tag;
    in.filesize = (hg_size_t)st->filesize;
    in.err      = (int32_t)  st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    filesize_response_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static void filesize_response_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES response_forward\n", glb_pmi_rank);  fflush(stdout);
    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* send up to parent if we have gotten all replies */
    if (st->num_responses == child_count) {
        /* lookup max file offset we have for this file id */
        size_t filesize = 0;
        int ret = unifyfs_inode_get_extent_size(st->gfid, &filesize);
        if (ret) {
            /* TODO: handle ENOENT */
            st->err = ret;
        }

        /* update filesize in state struct if ours is bigger */
        if (filesize > st->filesize) {
            st->filesize = filesize;
        }

        /* send result to parent if we have one */
        if (parent != -1) {
            printf("%d: BUCKEYES filesize is %llu\n",
                glb_pmi_rank, (unsigned long long) st->filesize);
            fflush(stdout);
            rpc_invoke_filesize_response(parent, st);

            /* free state */
            // TODO: need to protect this code with pthread/margo locks
            int2void_delete(&glb_tag2state, st->tag);
            unifyfs_stack_push(glb_tag_stack, st->tag);
            unifyfs_coll_state_free(&st);
        } else {
            /* we're the root, deliver result back to client */
            printf("BUCKEYES filesize is %llu\n",
                (unsigned long long) st->filesize);
            fflush(stdout);

            /* to wake up requesting thread,
             * lock structure, signal condition variable, unlock */
            if (glb_pmi_size > 1) {
                /* if we had no children, then it's the main thread
                 * calling this function and we skip the condition
                 * variable to avoid deadlocking */
                ABT_mutex_lock(st->mutex);
                ABT_cond_signal(st->cond);
                ABT_mutex_unlock(st->mutex);
            }

            /* the requesting thread will release state strucutre
             * so we don't free it here */
        }
    }
}

void filesize_request_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES request_forward\n", glb_pmi_rank);
    fflush(stdout);

    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* invoke filesize request rpc on child */
        rpc_invoke_filesize_request(child, st);
    }

    /* if we are a leaf, get filesize and forward back to parent */
    if (child_count == 0) {
        filesize_response_forward(st);
    }
}

/* request a filesize operation to all servers for a given file
 * from a given server */
static void filesize_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    filesize_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup filesize
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int gfid     = (int) in.gfid;
    int32_t ptag = (int32_t) in.tag;

    /* build our output values */
    filesize_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* allocate a new structure to track state of this filesize operation,
     * we assign an integer tag to this structure which we pass to any process
     * that will later send us a response, that process will include this tag
     * in its response, and we use the tag value on that incoming message to
     * lookup the structure using a tag2state map */
    // TODO: protect these structures from concurrent rpcs with locking
    int32_t tag = unifyfs_stack_pop(glb_tag_stack);
    if (tag < 0) {
        // ERROR!
    }
    unifyfs_coll_state_t* st = unifyfs_coll_state_alloc(root, gfid, ptag, tag);
    int2void_add(&glb_tag2state, st->tag, (void*)st);

    /* forward request to children if needed */
    filesize_request_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_request_rpc)

/* allreduce of max filesize from each child */
static void filesize_response_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES response_rpc\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    filesize_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get tag which points to structure desribing the collective
     * this message is for, then get the filesize from this child */
    int32_t tag     = (int32_t) in.tag;
    size_t filesize = (size_t)  in.filesize;
    int err         = (int32_t) in.err;

    /* build our output values */
    filesize_response_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    unifyfs_coll_state_t* st = get_coll_state(tag);

    /* take the maximum of child's filesize and size stored in
     * our current state */
    if (filesize > st->filesize) {
        st->filesize = filesize;
    }

    /* bubble up error code to parent if child hit an error */
    if (err != UNIFYFS_SUCCESS) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    filesize_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_response_rpc)

/******************************************************************
 * truncate
 ******************************************************************/

static int rpc_invoke_truncate_request(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.truncate_request_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    truncate_request_in_t in;
    in.root   = (int32_t)st->root;
    in.tag    = (int32_t)st->tag;
    in.gfid   = (int32_t)st->gfid;
    in.length = (size_t) st->filesize;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    truncate_request_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_truncate_response(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.truncate_response_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    truncate_response_in_t in;
    in.tag = (int32_t) st->parent_tag;
    in.err = (int32_t) st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    truncate_response_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static void truncate_response_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES response_forward (truncate)\n", glb_pmi_rank);
    fflush(stdout);
    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* send up to parent if we have gotten all replies */
    if (st->num_responses == child_count) {
        /* truncate the file */
        int ret = unifyfs_inode_truncate(st->gfid, st->filesize);
        if (ret) {
            /* TODO: handle error */
            st->err = ret;
        }

        /* send result to parent if we have one */
        if (parent != -1) {
            printf("%d: BUCKEYES truncate(len=%llu)=%d\n",
                   glb_pmi_rank, (unsigned long long) st->filesize, ret);
            fflush(stdout);
            rpc_invoke_truncate_response(parent, st);

            /* free state */
            // TODO: need to protect this code with pthread/margo locks
            int2void_delete(&glb_tag2state, st->tag);
            unifyfs_stack_push(glb_tag_stack, st->tag);
            unifyfs_coll_state_free(&st);
        } else {
            /* we're the root, deliver result back to client */
            printf("BUCKEYES truncate(len=%llu)=%d, all complete err=%d\n",
                (unsigned long long) st->filesize, ret, st->err);
            fflush(stdout);

            /* to wake up requesting thread,
             * lock structure, signal condition variable, unlock */
            if (glb_pmi_size > 1) {
                /* if we had no children, then it's the main thread
                 * calling this function and we skip the condition
                 * variable to avoid deadlocking */
                ABT_mutex_lock(st->mutex);
                ABT_cond_signal(st->cond);
                ABT_mutex_unlock(st->mutex);
            }

            /* the requesting thread will release state strucutre
             * so we don't free it here */
        }
    }
}

void truncate_request_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES request_forward (truncate: len=%llu)\n",
           glb_pmi_rank, (unsigned long long) st->filesize);
    fflush(stdout);

    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* invoke truncate request rpc on child */
        rpc_invoke_truncate_request(child, st);
    }

    /* if we are a leaf, get truncate and forward back to parent */
    if (child_count == 0) {
        truncate_response_forward(st);
    }
}

/* request a truncate operation to all servers for a given file
 * from a given server */
static void truncate_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc (truncate)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup truncate
     * record tag calling process wants us to include in our
     * later response */
    int root      = (int) in.root;
    int gfid      = (int) in.gfid;
    int32_t ptag  = (int32_t) in.tag;
    size_t length = (size_t) in.length;

    /* build our output values */
    truncate_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    // TODO: protect these structures from concurrent rpcs with locking
    int32_t tag = unifyfs_stack_pop(glb_tag_stack);
    if (tag < 0) {
        // ERROR!
    }
    unifyfs_coll_state_t* st = unifyfs_coll_state_alloc(root, gfid, ptag, tag);
    st->filesize = length;
    int2void_add(&glb_tag2state, st->tag, (void*)st);

    /* forward request to children if needed */
    truncate_request_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(truncate_request_rpc)

/* allreduce of max truncate from each child */
static void truncate_response_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES response_rpc (truncate)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get tag which points to structure desribing the collective
     * this message is for, then get the truncate from this child */
    int32_t tag = (int32_t) in.tag;
    int err     = (int32_t) in.err;

    /* build our output values */
    truncate_response_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    unifyfs_coll_state_t* st = get_coll_state(tag);

    /* bubble up error code to parent if child hit an error */
    if (err != UNIFYFS_SUCCESS) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    truncate_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(truncate_response_rpc)

/******************************************************************
 * unlink
 ******************************************************************/

static int rpc_invoke_unlink_request(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.unlink_request_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    unlink_request_in_t in;
    in.root = (int32_t) st->root;
    in.tag  = (int32_t) st->tag;
    in.gfid = (int32_t) st->gfid;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    unlink_request_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_unlink_response(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.unlink_response_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    unlink_response_in_t in;
    in.tag = (int32_t) st->parent_tag;
    in.err = (int32_t) st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    unlink_response_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static void unlink_response_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES response_forward (unlink)\n", glb_pmi_rank);
    fflush(stdout);
    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* send up to parent if we have gotten all replies */
    if (st->num_responses == child_count) {
        /* unlink the file */
        int ret = unifyfs_inode_unlink(st->gfid);
        if (ret) {
            /* TODO: handle error */
            st->err = ret;
        }

        /* send result to parent if we have one */
        if (parent != -1) {
            printf("%d: BUCKEYES unlink=%d\n", glb_pmi_rank, ret);
            fflush(stdout);
            rpc_invoke_unlink_response(parent, st);

            /* free state */
            // TODO: need to protect this code with pthread/margo locks
            int2void_delete(&glb_tag2state, st->tag);
            unifyfs_stack_push(glb_tag_stack, st->tag);
            unifyfs_coll_state_free(&st);
        } else {
            /* we're the root, deliver result back to client */
            printf("BUCKEYES unlink=%d, all complete err=%d\n", ret, st->err);
            fflush(stdout);

            /* to wake up requesting thread,
             * lock structure, signal condition variable, unlock */
            if (glb_pmi_size > 1) {
                /* if we had no children, then it's the main thread
                 * calling this function and we skip the condition
                 * variable to avoid deadlocking */
                ABT_mutex_lock(st->mutex);
                ABT_cond_signal(st->cond);
                ABT_mutex_unlock(st->mutex);
            }

            /* the requesting thread will release state strucutre
             * so we don't free it here */
        }
    }
}

void unlink_request_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES request_forward (unlink)\n", glb_pmi_rank);
    fflush(stdout);

    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* invoke unlink request rpc on child */
        rpc_invoke_unlink_request(child, st);
    }

    /* if we are a leaf, get unlink and forward back to parent */
    if (child_count == 0) {
        unlink_response_forward(st);
    }
}

/* request a unlink operation to all servers for a given file
 * from a given server */
static void unlink_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc (unlink)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    unlink_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup unlink
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int gfid     = (int) in.gfid;
    int32_t ptag = (int32_t) in.tag;

    /* build our output values */
    unlink_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    // TODO: protect these structures from concurrent rpcs with locking
    int32_t tag = unifyfs_stack_pop(glb_tag_stack);
    if (tag < 0) {
        // ERROR!
    }
    unifyfs_coll_state_t* st = unifyfs_coll_state_alloc(root, gfid, ptag, tag);
    int2void_add(&glb_tag2state, st->tag, (void*)st);

    /* forward request to children if needed */
    unlink_request_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(unlink_request_rpc)

/* allreduce of max unlink from each child */
static void unlink_response_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES response_rpc (unlink)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    unlink_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get tag which points to structure desribing the collective
     * this message is for, then get the unlink from this child */
    int32_t tag = (int32_t) in.tag;
    int err     = (int32_t) in.err;

    /* build our output values */
    unlink_response_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    unifyfs_coll_state_t* st = get_coll_state(tag);

    /* bubble up error code to parent if child hit an error */
    if (err != UNIFYFS_SUCCESS) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    unlink_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(unlink_response_rpc)

/******************************************************************
 * metaset 
 ******************************************************************/

static int rpc_invoke_metaset_request(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.metaset_request_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    unifyfs_file_attr_t *attr = &st->attr;
    metaset_request_in_t in;
    in.root   = (int32_t) st->root;
    in.tag    = (int32_t) st->tag;
    in.create = (int32_t) st->create;
    in.attr   = *attr;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    metaset_request_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_metaset_response(int rank, unifyfs_coll_state_t* st)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.metaset_response_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    metaset_response_in_t in;
    in.tag = (int32_t) st->parent_tag;
    in.err = (int32_t) st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* wait for rpc output */
    metaset_response_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static void metaset_response_forward(unifyfs_coll_state_t* st)
{
    printf("%d: BUCKEYES response_forward (metaset)\n", glb_pmi_rank);
    fflush(stdout);
    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* send up to parent if we have gotten all replies */
    if (st->num_responses == child_count) {
        /* metaset the file */
        int ret = unifyfs_inode_metaset(st->gfid, st->create, &st->attr);
        if (ret) {
            /* TODO: handle error */
            st->err = ret;
        }

        /* send result to parent if we have one */
        if (parent != -1) {
            printf("%d: BUCKEYES metaset=%d\n", glb_pmi_rank, ret);
            fflush(stdout);
            rpc_invoke_metaset_response(parent, st);

            /* free state */
            // TODO: need to protect this code with pthread/margo locks
            int2void_delete(&glb_tag2state, st->tag);
            unifyfs_stack_push(glb_tag_stack, st->tag);
            unifyfs_coll_state_free(&st);
        } else {
            /* we're the root, deliver result back to client */
            printf("BUCKEYES metaset=%d, all complete err=%d\n", ret, st->err);
            fflush(stdout);

            /* to wake up requesting thread,
             * lock structure, signal condition variable, unlock */
            if (glb_pmi_size > 1) {
                /* if we had no children, then it's the main thread
                 * calling this function and we skip the condition
                 * variable to avoid deadlocking */
                ABT_mutex_lock(st->mutex);
                ABT_cond_signal(st->cond);
                ABT_mutex_unlock(st->mutex);
            }

            /* the requesting thread will release state strucutre
             * so we don't free it here */
        }
    }
}

void metaset_request_forward(unifyfs_coll_state_t* st)
{
    /* get tree we're using for this operation */
    unifyfs_tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    printf("%d: BUCKEYES request_forward (metaset:%s)\n",
           glb_pmi_rank, st->attr.filename);
    fflush(stdout);


    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* invoke metaset request rpc on child */
        rpc_invoke_metaset_request(child, st);
    }

    /* if we are a leaf, get metaset and forward back to parent */
    if (child_count == 0) {
        metaset_response_forward(st);
    }
}

/* request a metaset operation to all servers for a given file
 * from a given server */
static void metaset_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc (metaset)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaset_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup metaset
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int32_t ptag = (int32_t) in.tag;
    int create   = (int) in.create;

    unifyfs_file_attr_t fattr = { 0, };
    fattr = in.attr;

    /* build our output values */
    metaset_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    // TODO: protect these structures from concurrent rpcs with locking
    int32_t tag = unifyfs_stack_pop(glb_tag_stack);
    if (tag < 0) {
        // ERROR!
    }
    unifyfs_coll_state_t* st = unifyfs_coll_state_alloc(root, fattr.gfid, ptag,
                                                        tag);
    st->create = create;
    st->attr = fattr;

    int2void_add(&glb_tag2state, st->tag, (void*)st);

    /* forward request to children if needed */
    metaset_request_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(metaset_request_rpc)

/* allreduce of max metaset from each child */
static void metaset_response_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES response_rpc (metaset)\n", glb_pmi_rank);
    fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaset_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get tag which points to structure desribing the collective
     * this message is for, then get the metaset from this child */
    int32_t tag = (int32_t) in.tag;
    int err     = (int32_t) in.err;

    /* build our output values */
    metaset_response_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    unifyfs_coll_state_t* st = get_coll_state(tag);

    /* bubble up error code to parent if child hit an error */
    if (err != UNIFYFS_SUCCESS) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    metaset_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(metaset_response_rpc)


/*
 * Broadcast operation for file extends
 */
typedef struct {
    margo_request request;
    hg_handle_t   handle;
} unifyfs_coll_request_t;

static int rpc_allocate_extbcast_handle(int rank, unifyfs_coll_request_t *request)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.extbcast_request_id, &request->handle);
    assert(hret == HG_SUCCESS);

    return rc;
}

static int rpc_invoke_extbcast_request(extbcast_request_in_t *in, unifyfs_coll_request_t *request)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_iforward(request->handle, in, &request->request);
    assert(hret == HG_SUCCESS);

    return rc;
}

/**
 * @brief Blocking function to forward extent broadcast request
 * 
 * @param broadcast_tree The tree for the broadcast
 * @param in Input data for the broadcast
 * @return int
 */
static int extbcast_request_forward(const unifyfs_tree_t* broadcast_tree, extbcast_request_in_t *in)
{
    printf("%d: BUCKEYES request_forward\n", glb_pmi_rank);
    fflush(stdout);

    hg_return_t hret;
    int ret;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    unifyfs_coll_request_t *requests = malloc(sizeof(unifyfs_coll_request_t) * child_count);
    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* allocate handle */
        ret = rpc_allocate_extbcast_handle(child, &requests[i]);

        /* invoke filesize request rpc on child */
        ret = rpc_invoke_extbcast_request(in, &requests[i]);
    }

    /* wait for the requests to finish */
    extbcast_request_out_t out;
    for (i = 0; i < child_count; i++) {
        /* TODO: get outputs */
        hret = margo_wait(requests[i].request);

        /* get the output of the rpc */
        hret = margo_get_output(requests[i].handle, &out);

        /* set return value
         * TODO: check if we have an error and handle it */
        ret = out.ret;
    }

    return ret;
}

#define UNIFYFS_BCAST_K_ARY 2

/* request a filesize operation to all servers for a given file
 * from a given server */
static void extbcast_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc (extbcast)\n", glb_pmi_rank);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get instance id */
    margo_instance_id mid = margo_hg_handle_get_instance(handle);

    /* get input params */
    extbcast_request_in_t in;
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup filesize
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int gfid     = (int) in.gfid;
    int32_t ptag = (int32_t) in.tag;
    int32_t num_extents = (int32_t) in.num_extends;

    /* allocate memory for extends */
    struct extent_tree_node* extents;
    extents = calloc(num_extents, sizeof(struct extent_tree_node));

    /* get client address */
    const struct hg_info* info = margo_get_info(handle);
    hg_addr_t client_address = info->addr;

    hg_size_t buf_size = num_extents*sizeof(struct extent_tree_node);

    /* expose local bulk buffer */
    hg_bulk_t extent_data;
    void *datap = extents;
    margo_bulk_create(mid, 1, &datap, &buf_size,
                      HG_BULK_READWRITE, &extent_data);

    /* request for bulk transfer */
    margo_request bulk_request;

    /* initiate data transfer
     * TODO: see if we can make this asynchronous */
    margo_bulk_itransfer(mid, HG_BULK_PULL, client_address,
                         in.exttree, 0,
                         extent_data, 0,
                         buf_size,
                         &bulk_request);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* update input structure to point to local bulk handle */
    in.exttree = extent_data;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    unifyfs_coll_request_t *requests = malloc(sizeof(unifyfs_coll_request_t) * bcast_tree.child_count);

    /* allogate mercury handles for forwarding the request */
    int i;
    for (i = 0; i < bcast_tree.child_count; i++) {
        /* get rank of this child */
        int child = bcast_tree.child_ranks[i];
        /* allocate handle */
        ret = rpc_allocate_extbcast_handle(child, &requests[i]);
    }

    /* wait for bulk request to finish */
    hret = margo_wait(bulk_request);

    LOGDBG("received %d extents (%lu bytes) from %d",
           num_extents, buf_size, root);

    /* forward request down the tree */
    for (i = 0; i < bcast_tree.child_count; i++) {
        /* invoke filesize request rpc on child */
        ret = rpc_invoke_extbcast_request(&in, &requests[i]);
    }

    ret = unifyfs_inode_add_shadow_extents(gfid, num_extents, extents);
    if (ret) {
        LOGERR("filling shadow extent failed (ret=%d)\n", ret);
        // what do we do now?
    }

    /* wait for the requests to finish */
    for (i = 0; i < bcast_tree.child_count; i++) {
        extbcast_request_out_t out;
        /* TODO: get outputs */
        hret = margo_wait(requests[i].request);

        /* get the output of the rpc */
        hret = margo_get_output(requests[i].handle, &out);

        /* set return value
         * TODO: check if we have an error and handle it */
        ret = out.ret;
    }

    /* build our output values */
    extbcast_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    free(requests);
}
DEFINE_MARGO_RPC_HANDLER(extbcast_request_rpc)

/**
 * @brief 
 * 
 * @return int UnifyFS return code
 */
int unifyfs_broadcast_extent_tree(int gfid)
{
    LOGDBG("%d: BUCKEYES unifyfs_broadcast_extend_tree\n", glb_pmi_rank);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;
    int root = glb_pmi_rank; /* root of the broadcast tree */

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    hg_size_t num_extents = 0;
    struct extent_tree_node *extents = NULL;

    ret = unifyfs_inode_get_local_extents(gfid, &num_extents, &extents);
    if (ret) {
        LOGERR("reading all extents failed (gfid=%d, ret=%d)\n", gfid, ret);
        // abort function?
    }

    hg_size_t buf_size = num_extents * sizeof(*extents);

    ret = unifyfs_inode_add_shadow_extents(gfid, num_extents, extents);
    if (ret) {
        LOGERR("filling shadow extent failed (gfid=%d, ret=%d)\n", gfid, ret);
        // what do we do now?
    }

    LOGDBG("broadcasting %lu extents (%lu bytes): ", num_extents, buf_size);

    /* create bulk data structure containing the extends
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_bulk_t extent_data;
    void *datap = (void *) extents;
    margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                      &datap, &buf_size,
                      HG_BULK_READ_ONLY, &extent_data);

    // get tag for collective
    // TODO: protect these structures from concurrent rpcs with locking
    int tag = unifyfs_stack_pop(glb_tag_stack);
    if (tag < 0) {
        // ERROR!
    }

    /* fill in input struct */
    extbcast_request_in_t in;
    in.root = (int32_t)glb_pmi_rank;
    in.tag  = (int32_t)tag;
    in.gfid = gfid;
    in.num_extends = num_extents;
    in.exttree = extent_data;

    /*  */
    extbcast_request_forward(&bcast_tree, &in);

    /* free bulk data handle */
    margo_bulk_free(extent_data);
    free(extents);

    return ret;
}

