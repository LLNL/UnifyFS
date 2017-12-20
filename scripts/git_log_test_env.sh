#!/bin/bash
#
# This script helps configure a test environment using git commit
# messages. Extract variable assignment statements beginning with TEST_
# from the git commit message and print them so the variables can be
# imported into the caller's environment. For example,
#
# eval $(git_log_test_env.sh)
#

git log HEAD^..HEAD | sed "s/^ *//g" | grep '^TEST_.*='
