#!/bin/bash

test_description="Test Metadata API"

#
# Source sharness environment scripts to pick up test environment
# and UnifyFS runtime settings.
#
. $(dirname $0)/sharness.d/00-test-env.sh
. $(dirname $0)/sharness.d/01-unifyfs-settings.sh

# create a new directory for MDHIM data
export UNIFYFS_META_DB_PATH=$UNIFYFS_TEST_TMPDIR/meta2
mkdir -p $UNIFYFS_META_DB_PATH

$UNIFYFS_BUILD_DIR/t/server/metadata.t
