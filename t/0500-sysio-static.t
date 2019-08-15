#!/bin/bash
#
# Source sharness environment scripts to pick up test environment
# and UnifyFS runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
. $(dirname $0)/sharness.d/01-unifyfs-settings.sh
$JOB_RUN_COMMAND $UNIFYFS_BUILD_DIR/t/sys/sysio-static.t
