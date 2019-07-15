#!/bin/sh

# This test check the mountpoints and then starts the unifycrd server. It then
# checks that the server is still running for all subsequent tests. If the
# servers fails to start, cleanup_hosts is called and testing is exited.

test_description="Start the UnifyCR server"

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
    test_expect_success "UNIFYCR_MOUNTPOINT ($UNIFYCR_MP) is real directory" '
        test_path_is_dir $UNIFYCR_MP ||
        { test_set_prereq FAIL; return 1; }
    '

    # If FAIL prereq is set, then UNIFYCR_MOUNTPOINT was set to a real dir, but
    # it is not a shared directory. If this is the case, no point in starting
    # servers to proceed with tests.
    if test_have_prereq FAIL; then
        say "UNIFYCR_MOUNTPOINT set to real dir; dir not found on all hosts."
        say "Exiting."
        test_done
    fi
else # ensure mountpoint is a non-real directory
    test_expect_success "UNIFYCR_MOUNTPOINT ($UNIFYCR_MP) is fake directory" '
        test_must_fail test_path_is_dir $UNIFYCR_MP
    '
fi

# If running posix tests, posix mountpoint needs to be a real, shared dir
# If it's not, prereq will not be set and posix tests will be skipped
test_expect_success TEST_POSIX "CI_POSIX_MP ($CI_POSIX_MP) is shared dir" '
    test_path_is_shared_dir $CI_POSIX_MP &&
    test_set_prereq POSIX
'

# Start the server
test_expect_success "unifycrd hasn't started yet" '
    process_is_not_running unifycrd 10
'

$UNIFYCR_BIN/unifycr start -c -d -S $UNIFYCR_SHAREDFS_DIR \
    -e $UNIFYCR_BIN/unifycrd &> ${UNIFYCR_LOG_DIR}/unifycr.start.out &

test_expect_success "unifycrd started" '
    process_is_running unifycrd 10 ||
    { test_set_prereq FAIL; return 1; }
'

# If FAIL prereq is set, then unifycrd failed to start.
# No point in proceeding with testing, so cleanup and exit early
if test_have_prereq FAIL; then
    say "unifycrd failed to start."
    say "Try setting longer wait time. Cleaning up and exiting."
    cleanup_hosts
    test_done
fi

# If unifycrd doesn't stay running running, set fail test prereq
test_expect_success "unifycrd hasn't died" '
    test_must_fail process_is_not_running unifycrd 10 ||
    { test_set_prereq FAIL; return 1; }
'

# If FAIL prereq is set, then unifycrd failed to stay running.
# No point in proceeding with testing, so cleanup and exit early
if test_have_prereq FAIL; then
    say "unifycrd failed to stay running. Cleaning up and exiting."
    cleanup_hosts
    test_done
fi

# unifycrd has started by this point. Set up a trap to cleanup the hosts in the
# event of an early/unexpected non-zero exit. This trap gets removed after the
# final cleanup_hosts is called in 990-stop-server.sh. This is to prevent the
# trap from triggering when the overall test suite exits with 1 if/when any
# tests have failed.
clean_fail() {
    echo >&2 "Error in $1. Cleaning hosts"
    cleanup_hosts
}
trap 'clean_fail $BASH_SOURCE' EXIT
