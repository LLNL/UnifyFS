#!/bin/bash

test_description="Cleanup test environment"

. $(dirname $0)/sharness.sh -v

test_expect_success "Cleanup" '
    unifyfsd_cleanup
'

test_done
