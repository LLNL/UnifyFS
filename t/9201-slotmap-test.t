#!/bin/bash
#
# Source sharness environment scripts to pick up test environment
# and UnifyFS runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
. $(dirname $0)/sharness.d/01-unifyfs-settings.sh
$UNIFYFS_BUILD_DIR/t/common/slotmap_test.t
