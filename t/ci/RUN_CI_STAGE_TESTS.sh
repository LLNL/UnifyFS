#!/bin/sh

# This script runs a full sweep of tests using the
# stage application to bring data into the UnifyFS file
# space and then back out.

# This script runs 002-start-server.sh several times, while
# typically changing the server configuration for each block
# of runs.

# Each test (generating a random file, copying into unifyfs,
# copying it back out, and then checking against the original)
# is done by 800-stage-tests.sh. This script sets up the
# server configurations (outer loop), iterates over file size
# (inner loop), running the 800- script each time.
# Then the final results are saved to a permanent directory
# and we exit the job.

test_description="Overall Multiconfiguration UnifyFS Stage tests"

SECONDS=0
start_time=$SECONDS
echo "Started RUN_CI_STAGE_TETS.sh @: $(date)"

# Set up UNIFYFS_CI_DIR if this script is called first
UNIFYFS_CI_DIR=${UNIFYFS_CI_DIR:-"$(dirname "$(readlink -fm $BASH_SOURCE)")"}

# test_done gets called in 990-stop-server.sh if this is not set.
# If not set, tests can be run individually
UNIFYFS_CI_TESTS_FULL_RUN=true

# If the user hasn't specified an overall test label,
# here's the boilerplate one.
if [ -z "$STAGE_TEST_OVERALL_CONFIG" ] ; then
    STAGE_TEST_OVERALL_CONFIG="UnifyFS CI Generic Stage Test"
fi

# Setup testing
source $UNIFYFS_CI_DIR/001-setup.sh

# Determine time setup took
setup_time=$SECONDS
echo "Setup time -- $(elapsed_time start_time setup_time)"

# Function for running the file size sweep run once for every
# server configuration
function single_server_sweep {
    source $UNIFYFS_CI_DIR/002-start-server.sh

    export STAGE_FILE_SIZE_IN_MB="100"
    source $UNIFYFS_CI_DIR/800-stage-tests.sh

    export STAGE_FILE_SIZE_IN_MB="250"
    source $UNIFYFS_CI_DIR/800-stage-tests.sh

    export STAGE_FILE_SIZE_IN_MB="1000"
    source $UNIFYFS_CI_DIR/800-stage-tests.sh

    export STAGE_FILE_SIZE_IN_MB="2000"
    source $UNIFYFS_CI_DIR/800-stage-tests.sh
}

# First configuration
export STAGE_TEST_SPECIFIC_CONFIG="8GB_spill"
export UNIFYFS_LOGIO_SPILL_SIZE=$((8 * $GB))
single_server_sweep

# Stop server with --allow-restart option if not final time stopping
source $UNIFYFS_CI_DIR/990-stop-server.sh --allow-restart

# Second server configuration
export STAGE_TEST_SPECIFIC_CONFIG="16GB_spill"
export UNIFYFS_LOGIO_SPILL_SIZE=$((16 * $GB))
single_server_sweep

# Other server configurations would go here, each one calling
# single_server_sweep() after setting environment variables appropriately
# Ensure that only the final configuration doesn't provide the
# --allow-restart option when stopping the servers

# Stop unifyfsd and cleanup
source $UNIFYFS_CI_DIR/990-stop-server.sh

# Determine time testing took
testing_time=$SECONDS
echo "Testing time -- $(elapsed_time setup_time testing_time)"

# Save off the timing files in a location that won't be deleted
mkdir -p ${UNIFYFS_CI_DIR}/stage_serial_test_timings \
   ${UNIFYFS_CI_DIR}/stage_parallel_test_timings
cp ${STAGE_LOG_DIR}/timings_serial_${JOB_ID}.dat \
   ${UNIFYFS_CI_DIR}/stage_serial_test_timings
cp ${STAGE_LOG_DIR}/timings_parallel_${JOB_ID}.dat \
   ${UNIFYFS_CI_DIR}/stage_parallel_test_timings

end_time=$SECONDS
echo "All done @ $(date)"
echo "Total run time -- $(elapsed_time start_time end_time)"

test_done
exit 0
