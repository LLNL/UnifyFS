// server headers
#include "unifyfs_global.h"
#include "unifyfs_tree.h"

#include "abt.h"
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
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

/* lookup function that maps tag to collective state object,
 * for now we only have the one global structure */
static unifyfs_state_filesize_t* get_state(int32_t tag)
{
    // TODO: lookup tag-->state entry in tag2state map, return state
    void* value = int2void_find(&glb_tag2state, tag);
    if (value == NULL) {
        // ERROR, not found
    }
    return (unifyfs_state_filesize_t*)value;
}

/* function to lookup local max write offset for given file id */
static size_t get_max_offset(int gfid)
{
    gfid2ext_tree_rdlock(&glb_gfid2ext);

    struct extent_tree* extents = gfid2ext_tree_extents(&glb_gfid2ext, gfid);

    /* TODO: if we don't have any extents for this file,
     * we should return an invalid file size */
    if (NULL == extents) {
        gfid2ext_tree_unlock(&glb_gfid2ext);
        return 0;
    }

    /* otherwise we have some actual extents,
     * return the max offset */
    size_t filesize = extent_tree_max(extents);

    /* the max value here is the offset of the last byte actually written
     * to, we want the filesize here, which would be one more than the last
     * offset since offsets are zero-based.  However, we can't just add one
     * because the above function also returns in the case where there are
     * no extents, thus we only add one if we have at least one extent */
    int segments = extent_tree_count(extents);
    if (segments > 0) {
        filesize += 1;
    }

    gfid2ext_tree_unlock(&glb_gfid2ext);

    return filesize;
}

static int rpc_invoke_filesize_request(int rank, unifyfs_state_filesize_t* st)
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

static int rpc_invoke_filesize_response(int rank, unifyfs_state_filesize_t* st)
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

unifyfs_state_filesize_t* state_filesize_alloc(
    int root,
    int gfid,
    int32_t ptag,
    int32_t tag)
{
    unifyfs_state_filesize_t* st = (unifyfs_state_filesize_t*)
        malloc(sizeof(unifyfs_state_filesize_t));
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

void state_filesize_free(unifyfs_state_filesize_t** pst)
{
    if (pst != NULL) {
        unifyfs_state_filesize_t* st = *pst;

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

static void filesize_response_forward(unifyfs_state_filesize_t* st)
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
        size_t filesize = get_max_offset(st->gfid);

        /* update filesize in state struct if ours is bigger */
        if (filesize > st->filesize) {
            st->filesize = filesize;
        }

        /* TODO: mark error if file size lookup failed */
        //if (lookup != UNIFYFS_SUCCESS) {
        //    st->err = UNIFYFS_ERROR_IO;
        //}

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
            state_filesize_free(&st);
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

void filesize_request_forward(unifyfs_state_filesize_t* st)
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
    unifyfs_state_filesize_t* st = state_filesize_alloc(root, gfid, ptag, tag);
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
    unifyfs_state_filesize_t* st = get_state(tag);

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
