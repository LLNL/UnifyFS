#!/bin/sh

# This test checks that the server terminated successfully, checks the
# mountpoints, and then cleans up the hosts (if desired).

test_description="Stopping the UnifyFS server"

CLEANUP_JOB_ON_EXIT="1"

for arg in "$@"
do
    case $arg in
        -h|--help)
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            $ci_dir/001-setup.sh -h
            exit
            ;;
	--allow-restart)
	    CLEANUP_AND_STOP_TESTING="0"
	    echo "Stop servers but allow restart and additional testing"
	    ;;
	*)
            echo "usage ./990-stop-server.sh -h|--help|--keep-job"
            exit 1
            ;;
    esac
done

test_expect_success "unifyfsd is still running" '
    process_is_running unifyfsd 10
'

$UNIFYFS_BIN/unifyfs terminate -d &> ${UNIFYFS_LOG_DIR}/unifyfs.terminate.out

test_expect_success "unifyfsd has stopped" '
    process_is_not_running unifyfsd 10
'

test_expect_success "verify unifyfsd has stopped" '
    test_must_fail process_is_running unifyfsd 10
'


if [ "$CLEANUP_JOB_ON_EXIT" -eq "1" ]; then


    # If UNIFYFS_MOUNTPOINT is an existing dir, verify that is it empty
    test_expect_success REAL_MP \
	  "Verify UNIFYFS_MOUNTPOINT ($UNIFYFS_MP) is empty" '
        test_dir_is_empty $UNIFYFS_MP
	'

    # Cleanup posix mountpoint
    test_expect_success POSIX \
	  "Cleanup UNIFYFS_CI_POSIX_MP: $UNIFYFS_CI_POSIX_MP" '
        rm -rf $UNIFYFS_CI_POSIX_MP/*posix*
	'

    # cleanup_hosts
    test_expect_success PDSH,CLEAN "Cleanup hosts" '
        cleanup_hosts
	'

    # Remove trap
    # If any tests failed, the suite will exit with 1 which
    # will trigger the trap.  Since the hosts were already
    # cleaned at this point, can remove trap to prevent
    # cleanup_hosts from being called again.
    trap - EXIT

    # end here if running tests individually
    if [[ -z $UNIFY_CI_TESTS_FULL_RUN ]]; then
    	( test_done; )
    	test_exit_code=$?

    	cd "$(dirname "$SHARNESS_TRASH_DIRECTORY")"

    	return $test_exit_code
    fi

fi # if [ $CLEANUP_ON_JOB_EXIT -eq "1" ]]; then
