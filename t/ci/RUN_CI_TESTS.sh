#!/bin/sh

# This script is to run the entire integration test suite of TAP tests for
# Unify.
# In order to run individual tests, run `./001-setup.sh -h`.
#
# To run all of the tests, simply run RUN_CI_TESTS.sh.
#
# E.g.:
#     $ ./RUN_CI_TESTS.sh
#   or
#     $ prove -v RUN_CI_TESTS.sh
#
# If individual tests are desired to be run, source the 001-setup.sh script
# first, followed by 002-start-server.sh. Then source each desired script after
# that preceded by `$UNIFYFS_CI_DIR`. When finished, source the
# 990-stop-server.sh script last.
#
# E.g.:
#      $ . full/path/to/001-setup.sh
#      $ . $UNIFYFS_CI_DIR/002-start-server.sh
#      $ . $UNIFYFS_CI_DIR/100-writeread-tests.sh
#      $ . $UNIFYFS_CI_DIR/990-stop-server.sh
#
# Before doing either of these, make sure you have interactively allocated nodes
# or are submitting a batch job.
#
# If additional tests are desired, create a script after the fashion of
# 100-writeread-tests.sh where the prefixed number indicates the desired order
# for running the tests. Then source that script in this script below, in the
# desired order.

test_description="Unify Integration Testing Suite"

RUN_CI_TESTS_USAGE="$(cat <<EOF
usage: ./RUN_CI_TESTS.sh -h|--help

This script is to run the entire integration test suite of TAP tests for Unify.
In order to run individual tests, run './001-setup.sh -h'.

To run all of the tests, simply run RUN_CI_TESTS.sh.

E.g.:
    $ ./RUN_CI_TESTS.sh
  or
    $ prove -v RUN_CI_TESTS.sh

You can run individually desired test files (i.e., 100-writeread-tests.sh and no
other tests) by first sourcing 001-setup.sh followed by 002-start-server.sh.
Then source any desired test files. Lastly, source 990-stop-server.sh.

E.g.:
    $ . full/path/to/001-setup.sh
    $ . \$UNIFYFS_CI_DIR/002-start-server.sh
    $ . \$UNIFYFS_CI_DIR/100-writeread-tests.sh
    $ . \$UNIFYFS_CI_DIR/990-stop-server.sh

Before doing either of these, make sure you have interactively allocated nodes
or are submitting a batch job.
EOF
)"

while [[ $# -gt 0 ]]
do
    case $1 in
        -h|--help)
            echo "$RUN_CI_TESTS_USAGE"
            exit
            ;;
        *)
            echo "usage: ./RUN_CI_TESTS.sh -h|--help"
            exit 1
            ;;
    esac
done

SECONDS=0
start_time=$SECONDS
echo "Started RUN_TESTS.sh @: $(date)"

# Set up UNIFYFS_CI_DIR if this script is called first
UNIFYFS_CI_DIR=${UNIFYFS_CI_DIR:-"$(dirname "$(readlink -fm $BASH_SOURCE)")"}

# test_done gets called in 990-stop-server.sh if this is not set.
# If not set, tests can be run individually
UNIFY_CI_TESTS_FULL_RUN=true

# setup testing
source $UNIFYFS_CI_DIR/001-setup.sh

# start unifyfsd
source $UNIFYFS_CI_DIR/002-start-server.sh

# determine time setup took
setup_time=$SECONDS
echo "Setup time -- $(elapsed_time start_time setup_time)"

##############################################################################
# Add additional testing files between here and the final testing time (before
# 990-stop-server.sh) in the desired order to run them.
##############################################################################

# POSIX-IO writeread example tests
source $UNIFYFS_CI_DIR/100-writeread-tests.sh

# POSIX-IO writeread example with I/O shuffle tests
source $UNIFYFS_CI_DIR/100-writeread-tests.sh --shuffle

# POSIX-IO write example tests
source $UNIFYFS_CI_DIR/110-write-tests.sh

# POSIX-IO read example tests
source $UNIFYFS_CI_DIR/120-read-tests.sh

# MPI-IO writeread example tests
source $UNIFYFS_CI_DIR/100-writeread-tests.sh --mpiio

# MPI-IO write example tests
source $UNIFYFS_CI_DIR/110-write-tests.sh --mpiio

# MPI-IO read example tests
source $UNIFYFS_CI_DIR/120-read-tests.sh --mpiio

### Producer-Consumer workload tests

# POSIX-IO producer-consumer tests
source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh

# MPI-IO producer-consumer tests
source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh --mpiio

##############################################################################
# DO NOT add additional tests after this point
##############################################################################
# determine time testing took
testing_time=$SECONDS
echo "Testing time -- $(elapsed_time setup_time testing_time)"

# stop unifyfsd and cleanup
source $UNIFYFS_CI_DIR/990-stop-server.sh

end_time=$SECONDS
echo "All done @ $(date)"
echo "Total run time -- $(elapsed_time start_time end_time)"

test_done
exit 0
