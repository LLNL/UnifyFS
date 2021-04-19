#!/bin/sh

# This script runs a full sweep of tests using the
# stage application to bring data into the UnifyFS file
# space and then back out.

# This script assumes that the 001-setup.sh has been run,
# and other initialization (like setting up spack
# environment and links).  This script runs
# 002-start-server.sh several times, while typically
# changing the server configuration for each block
# of runs.

# each test (generating a random file, copying into unifyfs,
# copying it back out, and then checking against the original)
# is done by 200-stage-tests.sh. This script sets up the
# server configurations (outer loop), iterates over file size
# (inner loop), running the 200- script each time.
# Then the final results are saved to a permanent directory
# and we exit the job.

# If the user hasn't specified an overall test label,
# here's the boilerplate one.
if [ -z "$STAGE_TEST_OVERALL_CONFIG" ] ; then
    STAGE_TEST_OVERALL_CONFIG="UnifyFS CI Generic Stage Test"
fi

test_description="Overall Multiconfiguration UnifyFS Stage tests"

# function for running the file size sweep
# run once for every server configuration
#
# one optional argument.  If the argument is "yes"
# then the final shut-down-servers call will
# clean up (but shouldn't kill the job).
function single_server_sweep {
    . $SHARNESS_TEST_DIRECTORY/002-start-server.sh

    export TEST_FILE_SIZE_IN_MB="100"
    . $SHARNESS_TEST_DIRECTORY/200-stage-tests.sh

    export TEST_FILE_SIZE_IN_MB="250"
    . $SHARNESS_TEST_DIRECTORY/200-stage-tests.sh

    export TEST_FILE_SIZE_IN_MB="1000"
    . $SHARNESS_TEST_DIRECTORY/200-stage-tests.sh

    export TEST_FILE_SIZE_IN_MB="2000"
    . $SHARNESS_TEST_DIRECTORY/200-stage-tests.sh

    if [ $1 -eq "yes" ] ; then
	. $SHARNESS_TEST_DIRECTORY/990-stop-server.sh
    else
	. $SHARNESS_TEST_DIRECTORY/990-stop-server.sh --keep-job
    fi
}

# first configuration.  Stock server configuration;
# whatever is set up in the install.
export STAGE_TEST_SPECIFIC_CONFIG="stock"
single_server_sweep

# second server configuration. This one has a (presumably bigger)
# spill file.
export STAGE_TEST_SPECIFIC_CONFIG="10GB_spill"
export UNIFYFS_LOGIO_SPILL_SIZE="10000000000"
single_server_sweep

# other server configurations would go here, each one calling
# single_server_sweep after setting environment variables approprately.

# save off the timing files in a location that won't be deleted
mkdir -p ${SHARNESS_TEST_DIRECTORY}/stage_test_timings/
cp ${UNIFYFS_LOG_DIR}/timings_${JOB_ID}.dat \
   ${SHARNESS_TEST_DIRECTORY}/stage_test_timings/

# Close out remaining servers if any, and close out job
# this MUST be the last command because this terminates the jobs
# and cleans up the nodes.
. $SHARNESS_TEST_DIRECTORY/990-stop-server.sh
