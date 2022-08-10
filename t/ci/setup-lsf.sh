#!/bin/sh

[[ -z $LSB_DJOB_HOSTFILE ]] && \
    { echo >&2 "$errmsg Found jsrun; no allocated nodes detected"; exit 1; }

# Functions specific to LSF

# Get the compute hosts being used in the current allocation.
# Returns the unique hosts being used on LSF, exluding the launch host.
get_lsf_hosts()
{
    # NOTE: It's possible that some systems with LSF may place the user on the
    # first compute node rather than a launch node.
    local l_hosts=$(uniq $LSB_DJOB_HOSTFILE | tail -n +2)
    echo "$l_hosts"
}

# Gets the compute hosts being used in the current allocation and returns them
# as a list so they are usable by commands like pdsh.
# Returns the unique hosts being used on LSF as a list.
# (e.g., host1,host2,...,hostn)
get_hostlist()
{
    local l_hosts=$(get_lsf_hosts)
    # Replace spaces with commas
    local l_hostlist=$(echo $l_hosts | tr ' ' ',')
    echo $l_hostlist
}

# Variables specific to LSF
nnodes=$(get_lsf_hosts | wc -w)

# Define each resource set
nprocs=${UNIFYFS_CI_NPROCS:-1}
ncores=${UNIFYFS_CI_NCORES:-18}

# Total resource sets and how many per host
nrs_per_node=${UNIFYFS_CI_NRS_PER_NODE:-1}
nres_sets=${UNIFYFS_CI_NRES_SETS:-$(($nnodes * $nrs_per_node))}

if [[ $ncores -gt 20 ]]; then
    echo >&2 "$errmsg Number of cores-per-resource-set" \
             "(\$UNIFYFS_CI_NCORES=$ncores) needs to be <= 20."
    exit 1
fi

if (($nres_sets % $nrs_per_node)); then
    echo >&2 "$errmsg Total number of resource sets ($nres_sets) must be" \
        "divisible by resource-sets-per-node ($nrs_per_node). Set" \
        "\$UNIFYFS_CI_NRES_SETS and/or \$UNIFYFS_CI_NRS_PER_NODE accordingly."
    exit 1
fi

if [ $(($nrs_per_node * $ncores)) -gt 40 ]; then
    echo >&2 "$errmsg Number of cores-per-resource-set ($ncores) *"\
        "resource-sets-per-node ($nrs_per_node) = $(($nrs_per_node*$ncores))" \
        "needs to be <= 40. Set \$UNIFYFS_CI_NCORES and/or" \
        "\$UNIFYFS_CI_NRS_PER_NODE accordingly."
    exit 1
fi

if [ $(($nres_sets / $nrs_per_node)) -gt $nnodes ]; then
    echo >&2 "$errmsg At least $(($nres_sets/$nrs_per_node)) allocated nodes" \
        "required for $nres_sets total resource sets with $nrs_per_node" \
        "resource-sets-per-node. Only $nnodes node(s) detected."
    exit 1
fi

jsargs="-a ${nprocs} -c ${ncores} -r ${nrs_per_node}"

app_out="-o"
app_err="-k"
JOB_RUN_COMMAND="jsrun $jsargs -n ${nres_sets}"
JOB_RUN_ONCE_PER_NODE="jsrun -r 1"
JOB_ID=${JOB_ID:-$LSB_JOBID}

# Set up producer-consumer variables and functions when using two or more hosts
if [[ $nnodes -ge 2 ]]; then
    # Prereq for running 300-producer-consumer-tests.sh
    test_set_prereq MULTIPLE_HOSTS

    # Get the initial half of the hosts in the current allocation
    get_initial_hosts()
    {
        local l_hosts=$(get_lsf_hosts)
        local l_initial_hosts=$(echo "$l_hosts" | head -n $(($nnodes / 2 )))
        # Replace spaces with commas
        local l_initial_hostlist=$(echo $l_initial_hosts | tr ' ' ',')
        echo $l_initial_hostlist
    }

    # Get the latter half of the hosts in the current allocation
    get_latter_hosts()
    {
        local l_hosts=$(get_lsf_hosts)
        local l_latter_hosts=$(echo "$l_hosts" | tail -n $(($nnodes / 2 )))
        # Replace spaces with commas
        local l_latter_hostlist=$(echo $l_latter_hosts | tr ' ' ',')
        echo $l_latter_hostlist
    }

    exclude_option="-x"
    JOB_RUN_HALF_NODES="jsrun $jsargs -n $(($nres_sets/2))"
fi

echo "$infomsg ====================== LSF Job Info ======================"
echo "$infomsg ----------------------- Job Status -----------------------"
bquery -l $JOB_ID
echo "$infomsg ----------------------------------------------------------"
