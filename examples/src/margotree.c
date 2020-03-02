// mpicc -o margotree margotree.c -I`pwd`/install-sierra/include \
//   -L`pwd`/install-sierra/lib -Wl,-rpath,`pwd`/install-sierra/lib \
//   -lmargo -labt -lmercury
//
// bsub -nnodes 2
// jsrun -r 1 ./margotree

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "mpi.h"

#include "abt.h"
#include "margo.h"
#include "mercury.h"

typedef struct {
    //char* hostname;
    char* margo_svr_addr_str;
    hg_addr_t margo_svr_addr;
} server_info_t;

/* filesize_response_rpc (server => server)
 *
 * initiates filesize request operation */
MERCURY_GEN_PROC(filesize_request_in_t,
                 ((int32_t)(root))
                 ((int32_t)(degree))
                 ((int32_t)(reply))
                 ((int32_t)(tag)))
MERCURY_GEN_PROC(filesize_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_request_rpc)

/* filesize_response_rpc (server => server)
 *
 * response to filesize request */
MERCURY_GEN_PROC(filesize_response_in_t,
                 ((int32_t)(tag))
                 ((hg_size_t)(filesize))
                 ((int32_t)(err)))
MERCURY_GEN_PROC(filesize_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_response_rpc)

DECLARE_MARGO_RPC_HANDLER(filesize_request_noreply_rpc)
DECLARE_MARGO_RPC_HANDLER(filesize_response_noreply_rpc)

int glb_rank;
int glb_ranks;

hg_id_t glb_filesize_request_id;
hg_id_t glb_filesize_response_id;
hg_id_t glb_filesize_request_noreply_id;
hg_id_t glb_filesize_response_noreply_id;

margo_instance_id glb_mid;
char* glb_addrs;
server_info_t* glb_servers;

/* define tree structure */
typedef struct {
    int rank;         /* global rank of calling process */
    int ranks;        /* number of ranks in tree */
    int parent_rank;  /* parent rank, -1 if root */
    int child_count;  /* number of children */
    int* child_ranks; /* list of child ranks */
} tree_t;

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
void tree_init(
    int rank,  /* rank of calling process */
    int ranks, /* number of ranks in tree */
    int root,  /* rank of root of tree */
    int k,     /* degree of k-ary tree */
    tree_t* t) /* output tree structure */
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
            printf("Failed to allocate memory for child rank array\n");
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

void tree_free(tree_t* t)
{
    /* free child rank list */
    free(t->child_ranks);
    t->child_ranks = NULL;

    return;
}

/* this structure tracks the state of an outstanding file size
 * bcast/reduce collective, it is allocated when a new operation
 * is started and freed once the process compeltes its portion
 * of the operation */
typedef struct {
  int root;            /* root of the tree (server making request) */
  int degree;          /* degree of tree */
  tree_t tree;         /* tree structure for given root */
  int reply;           /* whether to wait on reply from rpc call */
  int32_t parent_tag;  /* tag we use in our reply to our parent */
  int32_t tag;         /* tag children will use in their replies to us */
  int num_responses;   /* number of replies we have received from children */
  size_t filesize;     /* running max of file size */
  int err;             /* returns UNIFYFS_SUCCESS/ERROR code on operation */
  ABT_mutex mutex;     /* argobots mutex to protect condition variable below */
  ABT_cond cond;       /* argobots condition variable root server waits on */
} state_filesize_t;

state_filesize_t* glb_st;

state_filesize_t* state_filesize_alloc(
    int root,
    int degree,
    int reply,
    int32_t ptag,
    int32_t tag)
{
    state_filesize_t* st = (state_filesize_t*)
        malloc(sizeof(state_filesize_t));

    st->root          = root;
    st->degree        = degree;
    st->reply         = reply;
    st->parent_tag    = ptag;
    st->tag           = tag;
    st->num_responses = 0;
    st->filesize      = 0;
    st->err           = 0;
    st->mutex         = ABT_MUTEX_NULL;
    st->cond          = ABT_COND_NULL;

    /* define tree to use for given root */
    tree_init(glb_rank, glb_ranks, root, degree,
        &st->tree);

    return st;
}

void state_filesize_free(state_filesize_t** pst)
{
    if (pst != NULL) {
        state_filesize_t* st = *pst;

        tree_free(&st->tree);

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

static int rpc_invoke_filesize_request(int rank, state_filesize_t* st)
{
    int rc = (int)0;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(glb_mid, addr,
        glb_filesize_request_id, &handle);

    /* fill in input struct */
    filesize_request_in_t in;
    in.root  = (int32_t)st->root;
    in.tag   = (int32_t)st->tag;
    in.reply = (int32_t)st->reply;

    /* call rpc function */
    hret = margo_forward(handle, &in);

    /* wait for rpc output */
    filesize_request_out_t out;
    hret = margo_get_output(handle, &out);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_filesize_response(int rank, state_filesize_t* st)
{
    int rc = (int)0;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(glb_mid, addr,
        glb_filesize_response_id, &handle);

    /* fill in input struct */
    filesize_response_in_t in;
    in.tag      = (int32_t)  st->parent_tag;
    in.filesize = (hg_size_t)st->filesize;
    in.err      = (int32_t)  st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);

    /* wait for rpc output */
    filesize_response_out_t out;
    hret = margo_get_output(handle, &out);

    /* decode response */
    rc = (int) out.ret;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_filesize_request_noreply(int rank, state_filesize_t* st)
{
    int rc = (int)0;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(glb_mid, addr,
        glb_filesize_request_noreply_id, &handle);

    /* fill in input struct */
    filesize_request_in_t in;
    in.root  = (int32_t)st->root;
    in.tag   = (int32_t)st->tag;
    in.reply = (int32_t)st->reply;

    /* call rpc function */
    hret = margo_forward(handle, &in);

    /* free resources */
    margo_destroy(handle);

    return rc;
}

static int rpc_invoke_filesize_response_noreply(int rank, state_filesize_t* st)
{
    int rc = (int)0;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create(glb_mid, addr,
        glb_filesize_response_noreply_id, &handle);

    /* fill in input struct */
    filesize_response_in_t in;
    in.tag      = (int32_t)  st->parent_tag;
    in.filesize = (hg_size_t)st->filesize;
    in.err      = (int32_t)  st->err;

    /* call rpc function */
    hret = margo_forward(handle, &in);

    /* free resources */
    margo_destroy(handle);

    return rc;
}

static void filesize_response_forward(state_filesize_t* st)
{
    //printf("%d: BUCKEYES response_forward\n", glb_rank);  fflush(stdout);
    /* get tree we're using for this operation */
    tree_t* t = &st->tree;

    /* get info for tree */
    int parent       = t->parent_rank;
    int child_count  = t->child_count;
    int* child_ranks = t->child_ranks;

    /* send up to parent if we have gotten all replies */
    if (st->num_responses == child_count) {
        /* lookup max file offset we have for this file id */
        size_t filesize = glb_rank * 100;

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
            //printf("%d: BUCKEYES filesize is %llu\n",
            //    glb_rank, (unsigned long long) st->filesize);
            //fflush(stdout);
            if (st->reply) {
              rpc_invoke_filesize_response(parent, st);
            } else {
              rpc_invoke_filesize_response_noreply(parent, st);
            }

            /* free state */
            state_filesize_free(&st);
        } else {
            /* we're the root, deliver result back to client */
            //printf("BUCKEYES filesize is %llu\n",
            //    (unsigned long long) st->filesize);
            //fflush(stdout);

            /* to wake up requesting thread,
             * lock structure, signal condition variable, unlock */
            if (glb_ranks > 1) {
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

void filesize_request_forward(state_filesize_t* st)
{
    //printf("%d: BUCKEYES request_forward\n", glb_rank);
    //fflush(stdout);

    /* get tree we're using for this operation */
    tree_t* t = &st->tree;

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
        if (st->reply) {
          rpc_invoke_filesize_request(child, st);
        } else {
          rpc_invoke_filesize_request_noreply(child, st);
        }
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
    //printf("%d: BUCKEYES request_rpc\n", glb_rank);
    //fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = 0;

    /* get input params */
    filesize_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);

    /* get root of tree and global file id to lookup filesize
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int degree   = (int) in.degree;
    int reply    = (int) in.reply;
    int32_t ptag = (int32_t) in.tag;

    /* build our output values */
    filesize_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* allocate a new structure to track state of this filesize operation,
     * we assign an integer tag to this structure which we pass to any process
     * that will later send us a response, that process will include this tag
     * in its response, and we use the tag value on that incoming message to
     * lookup the structure using a tag2state map */
    int tag = glb_rank;
    glb_st = state_filesize_alloc(root, degree, reply, ptag, tag);

    /* forward request to children if needed */
    filesize_request_forward(glb_st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_request_rpc)

/* allreduce of max filesize from each child */
static void filesize_response_rpc(hg_handle_t handle)
{
    //printf("%d: BUCKEYES response_rpc\n", glb_rank);
    //fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = 0;

    /* get input params */
    filesize_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);

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

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    state_filesize_t* st = glb_st;

    /* take the maximum of child's filesize and size stored in
     * our current state */
    if (filesize > st->filesize) {
        st->filesize = filesize;
    }

    /* bubble up error code to parent if child hit an error */
    if (err != 0) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    filesize_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_response_rpc)

/* request a filesize operation to all servers for a given file
 * from a given server */
static void filesize_request_noreply_rpc(hg_handle_t handle)
{
    //printf("%d: BUCKEYES request_rpc\n", glb_rank);
    //fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = 0;

    /* get input params */
    filesize_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);

    /* get root of tree and global file id to lookup filesize
     * record tag calling process wants us to include in our
     * later response */
    int root     = (int) in.root;
    int degree   = (int) in.degree;
    int reply    = (int) in.reply;
    int32_t ptag = (int32_t) in.tag;

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* allocate a new structure to track state of this filesize operation,
     * we assign an integer tag to this structure which we pass to any process
     * that will later send us a response, that process will include this tag
     * in its response, and we use the tag value on that incoming message to
     * lookup the structure using a tag2state map */
    int tag = glb_rank;
    glb_st = state_filesize_alloc(root, degree, reply, ptag, tag);

    /* forward request to children if needed */
    filesize_request_forward(glb_st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_request_noreply_rpc)

/* allreduce of max filesize from each child */
static void filesize_response_noreply_rpc(hg_handle_t handle)
{
    //printf("%d: BUCKEYES response_rpc\n", glb_rank);
    //fflush(stdout);

    /* assume we'll succeed */
    int32_t ret = 0;

    /* get input params */
    filesize_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);

    /* get tag which points to structure desribing the collective
     * this message is for, then get the filesize from this child */
    int32_t tag     = (int32_t) in.tag;
    size_t filesize = (size_t)  in.filesize;
    int err         = (int32_t) in.err;

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup state structure corresponding to this incoming rpc */
    state_filesize_t* st = glb_st;

    /* take the maximum of child's filesize and size stored in
     * our current state */
    if (filesize > st->filesize) {
        st->filesize = filesize;
    }

    /* bubble up error code to parent if child hit an error */
    if (err != 0) {
        st->err = err;
    }

    /* bump up number of replies we have gotten */
    st->num_responses++;

    /* send reseponse if it's ready */
    filesize_response_forward(st);
}
DEFINE_MARGO_RPC_HANDLER(filesize_response_noreply_rpc)

/* setup_remote_target - Initializes the server-server margo target */
static margo_instance_id setup_remote_target(void)
{
    /* initialize margo */
    const char margo_protocol[] = "ofi+verbs://";
    margo_instance_id mid = margo_init(margo_protocol, MARGO_SERVER_MODE, 1, 4);
    if (mid == MARGO_INSTANCE_NULL) {
        printf("margo_init(%s)\n", margo_protocol);
        return mid;
    }

    /* figure out what address this server is listening on */
    hg_addr_t addr_self;
    hg_return_t hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        printf("margo_addr_self()\n");
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }

    char self_string[128];
    hg_size_t self_string_sz = sizeof(self_string);
    hret = margo_addr_to_string(mid,
                                self_string, &self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        printf("margo_addr_to_string()\n");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    printf("margo RPC server: %s\n", self_string);
    margo_addr_free(mid, addr_self);

    /* publish rpc address of server for remote servers */
//    rpc_publish_remote_server_addr(self_string);
    printf("%d: %s\n", glb_rank, self_string);  fflush(stdout);
    MPI_Allgather(self_string, 128, MPI_CHAR, glb_addrs, 128, MPI_CHAR, MPI_COMM_WORLD);

    return mid;
}

/* margo_connect_servers
 *
 * Using address strings found in glb_servers, resolve
 * each peer server's margo address.
 */
int margo_connect_servers(margo_instance_id mid)
{
    int ret = 0;

    // block until a margo_svr key pair published by all servers
    MPI_Barrier(MPI_COMM_WORLD);

    size_t i;
    for (i = 0; i < glb_ranks; i++) {
        char* margo_addr_str = &glb_addrs[i*128];
        glb_servers[i].margo_svr_addr = HG_ADDR_NULL;
        glb_servers[i].margo_svr_addr_str = margo_addr_str;

        hg_return_t hret = margo_addr_lookup(mid,
                                 glb_servers[i].margo_svr_addr_str,
                                 &(glb_servers[i].margo_svr_addr));
        if (hret != HG_SUCCESS) {
            printf("server index=%zu - margo_addr_lookup(%s) failed\n",
                   i, margo_addr_str);
            ret = 1;
        }
    }

    // block until a margo_svr key pair published by all servers
    MPI_Barrier(MPI_COMM_WORLD);

    return ret;
}

int main(int argc, char* argv[])
{
  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &glb_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &glb_ranks);

  glb_addrs   = (char*) malloc(glb_ranks * 128);
  glb_servers = (server_info_t*) malloc(glb_ranks * sizeof(server_info_t));

  glb_mid = setup_remote_target();

  glb_filesize_request_id =
        MARGO_REGISTER(glb_mid, "filesize_request_rpc",
                       filesize_request_in_t, filesize_request_out_t,
                       filesize_request_rpc);

  glb_filesize_response_id =
        MARGO_REGISTER(glb_mid, "filesize_response_rpc",
                       filesize_response_in_t, filesize_response_out_t,
                       filesize_response_rpc);

  glb_filesize_request_noreply_id =
        MARGO_REGISTER(glb_mid, "filesize_request_noreply_rpc",
                       filesize_request_in_t, void,
                       filesize_request_noreply_rpc);
  margo_registered_disable_response(glb_mid, glb_filesize_request_noreply_id, HG_TRUE);

  glb_filesize_response_noreply_id =
        MARGO_REGISTER(glb_mid, "filesize_response_noreply_rpc",
                       filesize_response_in_t, void,
                       filesize_response_noreply_rpc);
  margo_registered_disable_response(glb_mid, glb_filesize_response_noreply_id, HG_TRUE);

  margo_connect_servers(glb_mid);

  for (int reply = 0; reply < 2; reply++) {
  for (int degree = 2; degree < 9; degree++) {

  MPI_Barrier(MPI_COMM_WORLD);

  if (glb_rank == 0) {
    double start = MPI_Wtime();

    int root = 0;
    int tag = glb_rank;
    glb_st = state_filesize_alloc(root, degree, reply, -1, tag);

    /* lock structure until we can wait on it */
    if (glb_ranks > 1) {
        /* init mutex and lock before we use them,
         * these are only needed on the root server */
        ABT_mutex_create(&glb_st->mutex);
        ABT_cond_create(&glb_st->cond);

        /* lock the mutex */
        ABT_mutex_lock(glb_st->mutex);
    }

    /* start the reduction to get the file size */
    filesize_request_forward(glb_st);

    /* wait on signal that reduction has completed */
    if (glb_ranks > 1) {
        ABT_cond_wait(glb_st->cond, glb_st->mutex);
    }

    /* have result at this point, get it */
    size_t filesize = glb_st->filesize;
    //printf("BUCKEYES got a filesize of %llu\n",
    //    (unsigned long long) glb_st->filesize);
    //fflush(stdout);

    /* release lock and free state */
    if (glb_ranks > 1) {
        ABT_mutex_unlock(glb_st->mutex);
    }

    state_filesize_free(&glb_st);

    double end = MPI_Wtime();
    printf("procs=%d degree=%d reply=%d secs=%f\n", glb_ranks, degree, reply, (end - start));
  }

  MPI_Barrier(MPI_COMM_WORLD);

  double start = MPI_Wtime();

  int times = 100;
  for (int i = 0; i < times; i++) {
    if (glb_rank == 0) {
  
      int root = 0;
      int tag = glb_rank;
      glb_st = state_filesize_alloc(root, degree, reply, -1, tag);
  
      /* lock structure until we can wait on it */
      if (glb_ranks > 1) {
          /* init mutex and lock before we use them,
           * these are only needed on the root server */
          ABT_mutex_create(&glb_st->mutex);
          ABT_cond_create(&glb_st->cond);
  
          /* lock the mutex */
          ABT_mutex_lock(glb_st->mutex);
      }
  
      /* start the reduction to get the file size */
      filesize_request_forward(glb_st);
  
      /* wait on signal that reduction has completed */
      if (glb_ranks > 1) {
          ABT_cond_wait(glb_st->cond, glb_st->mutex);
      }
  
      /* have result at this point, get it */
      size_t filesize = glb_st->filesize;
      //printf("BUCKEYES got a filesize of %llu\n",
      //    (unsigned long long) glb_st->filesize);
      //fflush(stdout);
  
      /* release lock and free state */
      if (glb_ranks > 1) {
          ABT_mutex_unlock(glb_st->mutex);
      }
  
      state_filesize_free(&glb_st);
    }
  }

  if (glb_rank == 0) {
    double end = MPI_Wtime();
    printf("procs=%d degree=%d reply=%d secs=%f\n", glb_ranks, degree, reply, (end - start) / (double)times);
  }

  }
  }

//  while(1) {
//    sleep(1);
//  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (glb_mid != MARGO_INSTANCE_NULL) {
    margo_finalize(glb_mid);
  }

  free(glb_servers);
  free(glb_addrs);
 
  MPI_Finalize();

  return 0;
}
