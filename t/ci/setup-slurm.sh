#!/bin/sh

[[ -z $SLURM_NNODES ]] && \
    { echo >&2 "$errmsg Found srun; no allocated nodes detected"; exit 1; }

# Functions specific to SLURM

# Get the compute hosts being used in the current allocation. Used with pdsh
# during cleanup after tests are done.
#
# Returns the unique hosts being used on SLURM.
get_hostlist()
{
    # In slurm, can do `pdsh -j $JOBID` to target all nodes in allocation,
    # however using nodelist for consistency of cleanup_hosts function when
    # using pdsh
    echo $SLURM_NODELIST
}

# Variables specific to SLURM
nnodes=$SLURM_NNODES
nres_sets=$SLURM_NNODES
nprocs=${UNIFYFS_CI_NPROCS:-1}

app_out="-o"
app_err="-e"
JOB_RUN_COMMAND="srun -N${nnodes} --ntasks-per-node=${nprocs}"
JOB_RUN_ONCE_PER_NODE="srun -n${nnodes}"
JOB_ID=${JOB_ID:-$SLURM_JOBID}

echo "$infomsg ====================== SLURM Job Info ======================"
echo "$infomsg ------------------------ Job Status ------------------------"
checkjob -v $JOB_ID
echo "$infomsg ------------------------------------------------------------"
