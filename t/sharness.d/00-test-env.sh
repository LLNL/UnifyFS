#
# Export some variables used by the test suite.
#
# UNIFYCR_BUILD_DIR is set to build path (based on current directory)
#
if test -z "$UNIFYCR_BUILD_DIR"; then
    if test -z "${builddir}"; then
        UNIFYCR_BUILD_DIR="$(cd .. && pwd)"
    else
        UNIFYCR_BUILD_DIR="$(cd ${builddir}/.. && pwd))"
    fi
    export UNIFYCR_BUILD_DIR
fi

#
# Name of script created during test run initialization
# to store dynamically generated paths for mountpoints and
# metadata directories.
#
export UNIFYCR_TEST_RUN_SCRIPT=$UNIFYCR_BUILD_DIR/t/test_run_env.sh

#
# Find MPI job launcher.
#
if test -n "$(which srun 2>/dev/null)"; then
    JOB_RUN_COMMAND="srun -n1 -N1"
elif test -n "$(which mpirun 2>/dev/null)"; then
    JOB_RUN_COMMAND="mpirun -wd $UNIFYCR_BUILD_DIR -np 1"
fi

if test -z "$JOB_RUN_COMMAND"; then
    echo >&2 "Failed to find a suitable parallel job launcher"
    echo >&2 "Do you need to install OpenMPI or SLURM?"
    return 1
fi

export JOB_RUN_COMMAND

#
# Set paths to executables
#
export UNIFYCRD=$UNIFYCR_BUILD_DIR/server/src/unifycrd
export TEST_WRITE_GOTCHA=$UNIFYCR_BUILD_DIR/client/tests/test_write_gotcha
export TEST_READ_GOTCHA=$UNIFYCR_BUILD_DIR/client/tests/test_read_gotcha
