#!/bin/bash
#
# Perform some initial setup for the test suite. This is not
# implemented as a sharness test because leaving a process running
# behind (i.e. unifyfsd) causes tap-driver.sh to hang waiting for
# the process to exit.
#

# Print TAP plan
echo 1..1

#
# Create temporary directories to be used as a common mount point and a
# common metadata directory across multiple tests. Save the value to a
# script in known location that later test scripts can source.
#
export UNIFYFS_MOUNT_POINT=$(mktemp -d)
export UNIFYFS_META_DB_PATH=$(mktemp -d)

#
# Source test environment first to pick up UNIFYFS_TEST_RUN_SCRIPT
#
. $(dirname $0)/sharness.d/00-test-env.sh

cat >"$UNIFYFS_TEST_RUN_SCRIPT" <<-EOF
export UNIFYFS_MOUNT_POINT=$UNIFYFS_MOUNT_POINT
export UNIFYFS_META_DB_PATH=$UNIFYFS_META_DB_PATH
EOF

. $(dirname $0)/sharness.d/01-unifyfs-settings.sh
. $(dirname $0)/sharness.d/02-functions.sh

#
# Start the UnifyFS daemon after killing and cleanup up after any previously
# running instance.
#
unifyfsd_stop_daemon
unifyfsd_cleanup
unifyfsd_start_daemon

#
# Make sure the unifyfsd process starts.
#
if ! process_is_running unifyfsd 5 ; then
    echo not ok 1 - unifyfsd started
    exit 1
fi

#
# Make sure unifyfsd stays running for 5 more seconds to catch cases where
# it dies during initialization.
#
if process_is_not_running unifyfsd 5; then
    echo not ok 1 - unifyfsd running
    exit 1
fi

#
# Make sure unifyfsd successfully generated client runstate file
#
uid=$(id -u)
if ! test -f $UNIFYFS_META_DB_PATH/unifyfs-runstate.conf.$uid ; then
    echo not ok 1 - unifyfsd runstate
    exit 1
fi

echo ok 1 - unifyfsd running
exit 0
