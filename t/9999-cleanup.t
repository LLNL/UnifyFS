#!/bin/bash

test_description="Cleanup test environment"

. $(dirname $0)/sharness.sh

test_expect_success "Cleanup" '
    unifyfsd_cleanup
'

test_done
