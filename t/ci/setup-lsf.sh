#!/bin/sh

[[ -z $LSB_DJOB_HOSTFILE ]] && \
    { echo >&2 "$errmsg Found jsrun; no allocated nodes detected"; exit 1; }

# Functions specific to LSF

# Get the compute hosts being used in the current allocation.
#
# Returns the unique hosts being used on LSF, exluding the launch host.
get_lsf_hosts()
{
    # NOTE: There's potential that some versions of LSF may change to where
    # they put the user on the first compute node rather than a launch node.
    local l_hosts=$(uniq $LSB_DJOB_HOSTFILE | tail -n +2)
    echo $l_hosts
}

# Gets the compute hosts being used in the current allocation and returns them
# as a list so they are usable by commands like pdsh.
#
# Returns the unique hosts being used on LSF as a list.
# (e.g., host1,host2,...,hostn)
get_hostlist()
{
    local l_hosts=$(get_lsf_hosts)
    #replace spaces with commas
    local l_hostlist=${l_hosts//[[:blank:]]/,}
    echo $l_hostlist
}

# Variables specific to LSF
nnodes=$(get_lsf_hosts | wc -w)

# Define each resource set
nprocs=${CI_NPROCS:-$nnodes}
ncores=${CI_NCORES:-20}

# Total resource sets and how many per host
nres_sets=${CI_NRES_SETS:-$nnodes}
nrs_per_node=${CI_NRS_PER_NODE:-1}

if [[ $ncores -gt 20 ]]; then
    echo >&2 "$errmsg Number of cores-per-resource-set (\$CI_NCORES=$ncores)" \
        "needs to be <= 20."
    exit 1
fi

if (($nres_sets % $nrs_per_node)); then
    echo >&2 "$errmsg Total number of resource sets ($nres_sets) must be" \
        "divisible by resource-sets-per-node ($nrs_per_node). Set" \
        "\$CI_NRES_SETS and/or \$CI_NRS_PER_NODE accordingly."
    exit 1
fi

if [ $(($nrs_per_node * $ncores)) -gt 40 ]; then
    echo >&2 "$errmsg Number of cores-per-resource-set ($ncores) *"\
        "resource-sets-per-node ($nrs_per_node) = $(($nrs_per_node*$ncores))" \
        "needs to be <= 40. Set \$CI_NCORES and/or \$CI_NRS_PER_NODE" \
        "accordingly."
    exit 1
fi

if [ $(($nres_sets / $nrs_per_node)) -gt $nnodes ]; then
    echo >&2 "$errmsg At least $(($nres_sets/$nrs_per_node)) allocated nodes" \
        "required for $nres_sets total resource sets with $nrs_per_node" \
        "resource-sets-per-node. Only $nnodes node(s) detected."
    exit 1
fi

jsargs="-a${nprocs} -c${ncores} -r${nrs_per_node} -n${nres_sets}"

app_out="-o"
app_err="-k"
JOB_RUN_COMMAND="jsrun $jsargs"
JOB_RUN_ONCE_PER_NODE="jsrun -r1"
JOB_ID=${JOB_ID:-$LSB_JOBID}

echo "$infomsg ====================== LSF Job Info ======================"
echo "$infomsg ----------------------- Job Status -----------------------"
bjobs -l $JOB_ID
echo "$infomsg ----------------------------------------------------------"
