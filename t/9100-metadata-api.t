#!/bin/bash

test_description="Test Metadata API"

#
# Source sharness environment scripts to pick up test environment
# and UnifyFS runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
$JOB_RUN_COMMAND $UNIFYFS_BUILD_DIR/t/server/metadata.t
