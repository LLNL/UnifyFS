#!/bin/sh

# This test check the mountpoints and then starts the unifyfsd server. It then
# checks that the server is still running for all subsequent tests. If the
# servers fails to start, cleanup_hosts is called and testing is exited.

test_description="Start the UnifyFS server"

while [[ $# -gt 0 ]]
do
    case $1 in
        -h|--help)
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            $ci_dir/001-setup.sh -h
            exit
            ;;
        *)
            echo "usage ./002-start-server.sh -h|--help"
            exit 1
            ;;
    esac
done

# Verify we have the mountpoints needed for testing
if test_have_prereq REAL_MP; then
    test_expect_success "UNIFYFS_MOUNTPOINT ($UNIFYFS_MP) is real directory" '
        test_path_is_dir $UNIFYFS_MP ||
        { test_set_prereq FAIL; return 1; }
    '

    # If FAIL prereq is set, then UNIFYFS_MOUNTPOINT was set to a real dir, but
    # it is not a shared directory. If this is the case, no point in starting
    # servers to proceed with tests.
    if test_have_prereq FAIL; then
        say "UNIFYFS_MOUNTPOINT set to real dir; dir not found on all hosts."
        say "Exiting."
        test_done
    fi
else # ensure mountpoint is a non-real directory
    test_expect_success "UNIFYFS_MOUNTPOINT ($UNIFYFS_MP) is fake directory" '
        test_must_fail test_path_is_dir $UNIFYFS_MP
    '
fi

# If running posix tests, posix mountpoint needs to be a real, shared dir
# If it's not, prereq will not be set and posix tests will be skipped
test_expect_success TEST_POSIX "POSIX_MP ($UNIFYFS_CI_POSIX_MP) is shared dir" '
    test_path_is_shared_dir $UNIFYFS_CI_POSIX_MP &&
    test_set_prereq POSIX
'

# Start the server
test_expect_success "unifyfsd hasn't started yet" '
    process_is_not_running unifyfsd 10
'

# UNIFYFS_BIN envar is set if not using unifyfs module
if [[ -n $UNIFYFS_BIN ]]; then
    $UNIFYFS_CLU start -d -S $UNIFYFS_SHAREDFS_DIR \
        -e $UNIFYFS_BIN/unifyfsd &> ${UNIFYFS_LOG_DIR}/unifyfs.start.out
else
    $UNIFYFS_CLU start -d -S $UNIFYFS_SHAREDFS_DIR \
        &> ${UNIFYFS_LOG_DIR}/unifyfs.start.out
fi

test_expect_success "unifyfsd started" '
    process_is_running unifyfsd 10 ||
    { test_set_prereq FAIL; return 1; }
'

# If FAIL prereq is set, then unifyfsd failed to start.
# No point in proceeding with testing, so cleanup and exit early
if test_have_prereq FAIL; then
    say "unifyfsd failed to start."
    say "Try setting longer wait time. Cleaning up and exiting."
    cleanup_hosts
    test_done
fi

# If unifyfsd doesn't stay running running, set fail test prereq
test_expect_success "unifyfsd hasn't died" '
    test_must_fail process_is_not_running unifyfsd 10 ||
    { test_set_prereq FAIL; return 1; }
'

# If FAIL prereq is set, then unifyfsd failed to stay running.
# No point in proceeding with testing, so cleanup and exit early
if test_have_prereq FAIL; then
    say "unifyfsd failed to stay running. Cleaning up and exiting."
    cleanup_hosts
    test_done
fi

# unifyfsd has started by this point. Set up a trap to cleanup the hosts in the
# event of an early/unexpected non-zero exit. This trap gets removed after the
# final cleanup_hosts is called in 990-stop-server.sh. This is to prevent the
# trap from triggering when the overall test suite exits with 1 if/when any
# tests have failed.
clean_fail() {
    echo >&2 "Error in $1. Cleaning hosts"
    cleanup_hosts
}
trap 'clean_fail $BASH_SOURCE' EXIT
