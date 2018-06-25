#!/bin/bash
#
# Perform some initial setup for the test suite. This is not
# implemented as a sharness test because leaving a process running
# behind (i.e. unifycrd) causes tap-driver.sh to hang waiting for
# the process to exit.
#

# Print TAP plan
echo 1..1

#
# Create temporary directories to be used as a common mount point and a
# common metadata directory across multiple tests. Save the value to a
# script in known location that later test scripts can source.
#
export UNIFYCR_MOUNT_POINT=$(mktemp -d)
export UNIFYCR_META_DB_PATH=$(mktemp -d)

#
# Source test environment first to pick up UNIFYCR_TEST_RUN_SCRIPT
#
. $(dirname $0)/sharness.d/00-test-env.sh

cat >"$UNIFYCR_TEST_RUN_SCRIPT" <<-EOF
export UNIFYCR_MOUNT_POINT=$UNIFYCR_MOUNT_POINT
export UNIFYCR_META_DB_PATH=$UNIFYCR_META_DB_PATH
EOF

. $(dirname $0)/sharness.d/01-unifycr-settings.sh
. $(dirname $0)/sharness.d/02-functions.sh

#
# Start the UnifyCR daemon after killing and cleanup up after any previously
# running instance.
#
unifycrd_stop_daemon
unifycrd_cleanup
unifycrd_start_daemon

#
# Make sure the unifycrd process starts.
#
if ! process_is_running unifycrd 5 ; then
    echo not ok 1 - unifycrd running
    exit 1
fi

#
# Make sure unifycrd stays running for 5 more seconds to catch cases where
# it dies during initialization.
#
if process_is_not_running unifycrd 5; then
    echo not ok 1 - unifycrd running
    exit 1
fi

#
# Make sure unifycrd successfully generated client runstate file
#
if ! test -f $UNIFYCR_META_DB_PATH/unifycr-runstate.conf ; then
    echo not ok 1 - unifycrd running
    exit 1
fi

echo ok 1 - unifycrd running
exit 0
