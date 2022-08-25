#!/bin/bash
#
# This test checks that I/O to the UnifyFS mount point was properly
# intercepted and redirected to the UnifyFS daemon.
#

# If this test fails, then at least one file made it to the UnifyFS mountpoint.
# This most likely means the corresponding wrapped function is failing in some
# way and the call is falling through to the operating system.
test_description="Verify UnifyFS intercepted mount point is empty"

. $(dirname $0)/sharness.sh -v

test_expect_success "Intercepted mount point $UNIFYFS_MOUNTPOINT is empty" '
    test_dir_is_empty $UNIFYFS_MOUNTPOINT
'

test_done
