#!/bin/bash

test_description="Shut down unifycrd"

. $(dirname $0)/sharness.sh

test_expect_success "Stop unifycrd" '
    unifycrd_stop_daemon
    process_is_not_running unifycrd 5
'

test_done
