#
# Export some variables used by the test suite.
#
# UNIFYFS_BUILD_DIR is set to build path (based on current directory)
#
if test -z "$UNIFYFS_BUILD_DIR"; then
    if test -z "${builddir}"; then
        UNIFYFS_BUILD_DIR="$(cd .. && pwd)"
    else
        UNIFYFS_BUILD_DIR="$(cd ${builddir}/.. && pwd))"
    fi
    export UNIFYFS_BUILD_DIR
fi

#
# Name of script created during test run initialization
# to store dynamically generated paths for mountpoints and
# metadata directories.
#
export UNIFYFS_TEST_RUN_SCRIPT=$UNIFYFS_BUILD_DIR/t/test_run_env.sh

#
# Find MPI job launcher.
#
if test -n "$(which jsrun 2>/dev/null)"; then
    JOB_RUN_COMMAND="jsrun -r1 -n1"
elif test -n "$(which srun 2>/dev/null)"; then
    JOB_RUN_COMMAND="srun -n1 -N1 --overlap"
elif test -n "$(which mpirun 2>/dev/null)"; then
    JOB_RUN_COMMAND="mpirun -np 1"
    if [ $UID -eq 0 ]; then
        mpirun --version |& fgrep 'Open MPI' >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            JOB_RUN_COMMAND="mpirun -np 1 --allow-run-as-root"
        fi
    fi
fi
if test -z "$JOB_RUN_COMMAND"; then
    echo >&2 "Failed to find a suitable parallel job launcher"
    echo >&2 "Do you need to install OpenMPI or SLURM?"
    return 1
fi
#echo >&2 "Using JOB_RUN_COMMAND: $JOB_RUN_COMMAND"
export JOB_RUN_COMMAND

#
# Set paths to executables
#
export UNIFYFSD=$UNIFYFS_BUILD_DIR/server/src/unifyfsd


# On systems with YAMA kernel support, Mercury's shared memory NA
# requires cross-memory attach to be enabled:
#    sysctl -w kernel.yama.ptrace_scope=0
if [ -f /proc/sys/kernel/yama/ptrace_scope ]; then
    scope_val=`cat /proc/sys/kernel/yama/ptrace_scope`
    if [ $scope_val -ne 0 ]; then
        if [ $UID -eq 0 ]; then
            echo 0 > /proc/sys/kernel/yama/ptrace_scope 2>/dev/null ||
              echo >&2 "Failed to enable cross-memory attach for Mercury shmem"
        else
            echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope 2>/dev/null ||
              echo >&2 "Failed to enable cross-memory attach for Mercury shmem"
        fi
    fi
fi

# Want core dumps? uncomment following line
#ulimit -c unlimited
