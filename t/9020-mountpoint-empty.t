#!/bin/bash
#
# This test checks that I/O to the UnifyCR mount point was properly
# intercepted and redirected to the UnifyCR daemon.
#

# If this test fails, then at least one file made it to the UnifyCr mountpoint.
# This most likely means the corresponding wrapped function is failing in some
# way and the call is falling through to the operating system.
test_description="Verify UnifyCR intercepted mount point is empty"

. $(dirname $0)/sharness.sh

test_expect_success "Intercepted mount point $UNIFYCR_MOUNT_POINT is empty" '
    test_dir_is_empty $UNIFYCR_MOUNT_POINT
'

test_done
