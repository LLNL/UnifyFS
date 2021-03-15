#!/bin/bash
#
# Source sharness environment scripts to pick up test environment
# and UnifyFS runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
. $(dirname $0)/sharness.d/01-unifyfs-settings.sh

# disable statfs from returning UnifyFS! super magic value,
# return tmpfs magic instead
export UNIFYFS_CLIENT_SUPER_MAGIC=0

$JOB_RUN_COMMAND $UNIFYFS_BUILD_DIR/t/sys/statfs-static.t
