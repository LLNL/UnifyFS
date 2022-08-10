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
JOB_RUN_COMMAND="srun -N ${nnodes} --ntasks-per-node=${nprocs}"
JOB_RUN_ONCE_PER_NODE="srun -n ${nnodes}"
JOB_ID=${JOB_ID:-$SLURM_JOBID}

# Set up producer-consumer variables and functions when using two or more hosts
if [[ $nnodes -ge 2 ]]; then
    # Prereq for running 300-producer-consumer-tests.sh
    test_set_prereq MULTIPLE_HOSTS

    # Get list of unique host names
    # Only want to run once as order can change each time
    # One alternative is to parse SLURM_NODELIST, which can get ugly
    SLURM_HOSTS=$($JOB_RUN_ONCE_PER_NODE hostname)

    # Get the initial half of the hosts in the current allocation
    get_initial_hosts()
    {
        local l_initial_hosts=$(echo "$SLURM_HOSTS" | head -n $(($nnodes / 2)))
        # Replace spaces with commas
        local l_initial_hostlist=$(echo $l_initial_hosts | tr ' ' ',')
        echo $l_initial_hostlist
    }

    # Get the latter half of the hosts in the current allocation
    get_latter_hosts()
    {
        local l_latter_hosts=$(echo "$SLURM_HOSTS" | tail -n $(($nnodes / 2)))
        # Replace spaces with commas
        local l_latter_hostlist=$(echo $l_latter_hosts | tr ' ' ',')
        echo $l_latter_hostlist
    }

    exclude_option="-x"
    JOB_RUN_HALF_NODES="srun -N $(($nnodes/2)) --ntasks-per-node=${nprocs}"
fi

echo "$infomsg ====================== SLURM Job Info ======================"
echo "$infomsg ------------------------ Job Status ------------------------"
scontrol show job $JOB_ID
echo "$infomsg ------------------------------------------------------------"
