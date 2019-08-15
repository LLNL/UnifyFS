#!/bin/bash

test_description="Shut down unifyfsd"

. $(dirname $0)/sharness.sh

test_expect_success "Stop unifyfsd" '
    unifyfsd_stop_daemon
    process_is_not_running unifyfsd 5
'

test_done
