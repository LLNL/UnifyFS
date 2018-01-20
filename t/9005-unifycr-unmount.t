#!/bin/bash
#
# Source sharness environment scripts to pick up test environment
# and UnifyCR runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
. $(dirname $0)/sharness.d/01-unifycr-settings.sh
$UNIFYCR_BUILD_DIR/t/unifycr_unmount.t
