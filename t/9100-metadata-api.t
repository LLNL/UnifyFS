#!/bin/bash

test_description="Test Metadata API"

#
# Source sharness environment scripts to pick up test environment
# and UnifyCR runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
$JOB_RUN_COMMAND -n 1 $UNIFYCR_BUILD_DIR/t/server/metadata.t
