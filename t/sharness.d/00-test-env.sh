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
    JOB_RUN_COMMAND="srun -n1 -N1"
elif test -n "$(which mpirun 2>/dev/null)"; then
    JOB_RUN_COMMAND="mpirun -wd $UNIFYFS_BUILD_DIR -np 1"
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
export TEST_WRITE_GOTCHA=$UNIFYFS_BUILD_DIR/client/tests/test_write_gotcha
export TEST_READ_GOTCHA=$UNIFYFS_BUILD_DIR/client/tests/test_read_gotcha
