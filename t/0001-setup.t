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
export UNIFYFS_TEST_TMPDIR=$(mktemp -d)
mkdir -p $UNIFYFS_TEST_TMPDIR/{meta,mount,share,spill,state}
export UNIFYFS_TEST_META=$UNIFYFS_TEST_TMPDIR/meta
export UNIFYFS_TEST_MOUNT=$UNIFYFS_TEST_TMPDIR/mount
export UNIFYFS_TEST_SHARE=$UNIFYFS_TEST_TMPDIR/share
export UNIFYFS_TEST_SPILL=$UNIFYFS_TEST_TMPDIR/spill
export UNIFYFS_TEST_STATE=$UNIFYFS_TEST_TMPDIR/state

#
# Source test environment first to pick up UNIFYFS_TEST_RUN_SCRIPT
#
. $(dirname $0)/sharness.d/00-test-env.sh

cat >"$UNIFYFS_TEST_RUN_SCRIPT" <<-EOF
export UNIFYFS_TEST_TMPDIR=$UNIFYFS_TEST_TMPDIR
export UNIFYFS_TEST_META=$UNIFYFS_TEST_META
export UNIFYFS_TEST_MOUNT=$UNIFYFS_TEST_MOUNT
export UNIFYFS_TEST_SHARE=$UNIFYFS_TEST_SHARE
export UNIFYFS_TEST_SPILL=$UNIFYFS_TEST_SPILL
export UNIFYFS_TEST_STATE=$UNIFYFS_TEST_STATE
EOF

. $(dirname $0)/sharness.d/01-unifyfs-settings.sh
. $(dirname $0)/sharness.d/02-functions.sh

#
# Start the UnifyFS daemon after killing any previously
# running instance.
#
unifyfsd_stop_daemon
unifyfsd_start_daemon

#
# Make sure the unifyfsd process starts.
#
if ! process_is_running unifyfsd 5 ; then
    cat $UNIFYFS_LOG_DIR/${UNIFYFS_LOG_FILE}* >&3
    echo not ok 1 - unifyfsd started
    exit 1
fi

#
# Make sure unifyfsd stays running for 5 more seconds to catch cases where
# it dies during initialization.
#
if process_is_not_running unifyfsd 5; then
    cat $UNIFYFS_LOG_DIR/${UNIFYFS_LOG_FILE}* >&3
    echo not ok 1 - unifyfsd running
    exit 1
fi

echo ok 1 - unifyfsd running
exit 0
