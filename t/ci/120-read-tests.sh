#!/bin/bash

# This contains all the tests for the read example.
#
# There are convenience functions such as `unify_run_test()` and get_filename()
# in the ci-functions.sh script that can make adding new tests easier. See the
# full UnifyFS documentatation for more info.
#
# There are multiple ways to to run an example using `unify_run_test()`
#     1. unify_run_test $app_name "$app_args" app_output
#     2. app_output=$(unify_run_test $app_name "app_args")
#
#     ---- Method 1
#     app_output=$(unify_run_test $app_name "$app_args")
#     rc=$?

#     echo "$app_output"
#     lcount=$(printf "%s" "$app_output" | wc -l)
#     ----

#     ---- Method 2
#     unify_run_test $app_name "$app_args" app_output
#     rc=$?
#
#     lcount=$(echo "$app_output" | wc -l)
#     ----
#
# The output of an example can then be tested with sharness, for example:
#
#     test_expect_success "$app_name $app_args: (line_count=$lcount, rc=$rc)" '
#         test $rc = 0 &&
#         test $lcount = 8
#     '
#
# For these tests, always include -b -c -n and -p in the app_args
# Then for the necessary tests, include -M -P -S -V or -x.


test_description="Read Tests"

while [[ $# -gt 0 ]]
do
    case $1 in
        -h|--help)
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            $ci_dir/001-setup.sh -h
            exit
            ;;
        *)
            echo "usage ./300-read-tests.sh -h|--help"
            exit 1
            ;;
    esac
done

# These two functions are simply to prevent code duplication since testing the
# output of each example with sharness is the same process. These do not need to
# be used, especially if wanting to test for something specific when running an
# example.
unify_test_read() {
    app_name=read-${1}

    # Run the test and get output
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    # Evaluate output
    test_expect_success "$app_name $app_args: (line_count=${lcount}, rc=$rc)" '
        test $rc = 0 &&
        test $lcount = 14
    '
}

unify_test_read_posix() {
    app_name=read-posix

    # Run the test and get output
    if test_have_prereq POSIX; then
        unify_run_test $app_name "$1" app_output
        rc=$?
        lcount=$(echo "$app_output" | wc -l)
    fi

    # Evaluate output
    test_expect_success POSIX "$app_name $1: (line_count=${lcount}, rc=$rc)" '
        test $rc = 0 &&
        test $lcount = 14
    '
}

# read-static -p n1 -n 2 -c 4KB -b 16KB
runmode=static
io_pattern="-p n1"
io_sizes="-n 2 -c $((4 * $KB)) -b $((16 * $KB))"
app_args="$io_pattern $io_sizes"
unify_test_read $runmode "$app_args"

# read-gotcha -p n1 -n 2 -c 4KB -b 16KB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-posix -p n1 -n 2 -c 4KB -b 16KB
runmode=posix
unify_test_read_posix "$app_args"

# Switch to -p nn
io_pattern="-p nn"
app_args="$io_pattern $io_sizes"

# read-posix -p nn -n 2 -c 4KB -b 16KB
unify_test_read_posix "$app_args"

# read-gotcha -p nn -n 2 -c 4KB -b 16KB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-static -p nn -n 2 -c 4KB -b 16KB
runmode=static
unify_test_read $runmode "$app_args"

# Increase sizes: -n 16 -c 32KB -b 1MB

# read-static -p nn -n 16 -c 32KB -b 1MB
io_sizes="-n 16 -c $((32 * $KB)) -b $MB"
app_args="$io_pattern $io_sizes"
unify_test_read $runmode "$app_args"

# read-gotcha -p nn -n 16 -c 32KB -b 1MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-posix -p nn -n 16 -c 32KB -b 1MB
runmode=posix
unify_test_read_posix "$app_args"

# Switch back to -p n1
io_pattern="-p n1"
app_args="$io_pattern $io_sizes"

# read-posix -p n1 -n 16 -c 32KB -b 1MB
unify_test_read_posix "$app_args"

# read-gotcha -p n1 -n 16 -c 32KB -b 1MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-static -p n1 -n 16 -c 32KB -b 1MB
runmode=static
unify_test_read $runmode "$app_args"

# Increase sizes: -n 32 -c 64KB -b 1MB

# read-static -p n1 -n 32 -c 64KB -b 1MB
io_sizes="-n 32 -c $((64 * $KB)) -b $MB"
app_args="$io_pattern $io_sizes"
unify_test_read $runmode "$app_args"

# read-gotcha -p n1 -n 32 -c 64KB -b 1MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-posix -p n1 -n 32 -c 64KB -b 1MB
runmode=posix
unify_test_read_posix "$app_args"

# Switch to -p nn
io_pattern="-p nn"
app_args="$io_pattern $io_sizes"

# read-posix -p nn -n 32 -c 64KB -b 1MB
unify_test_read_posix "$app_args"

# read-gotcha -p nn -n 32 -c 64KB -b 1MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-static -p nn -n 32 -c 64KB -b 1MB
runmode=static
unify_test_read $runmode "$app_args"

# Increase sizes: -n 64 -c 1MB -b 4MB

# read-static -p nn -n 64 -c 1MB -b 4MB
io_sizes="-n 64 -c $MB -b $((4 *  $MB))"
app_args="$io_pattern $io_sizes"
unify_test_read $runmode "$app_args"

# read-gotcha -p nn -n 64 -c 1MB -b 4MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-posix -p nn -n 64 -c 1MB -b 4MB
runmode=posix
unify_test_read_posix "$app_args"

# Switch back to -p n1
io_pattern="-p n1"
app_args="$io_pattern $io_sizes"

# read-posix -p n1 -n 64 -c 1MB -b 4MB
unify_test_read_posix "$app_args"

# read-gotcha -p n1 -n 64 -c 1MB -b 4MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-static -p n1 -n 64 -c 1MB -b 4MB
runmode=static
unify_test_read $runmode "$app_args"

# Increase sizes: -n 32 -c 1MB -b 16MB

# read-static -p n1 -n 32 -c 1MB -b 16MB
io_sizes="-n 32 -c $MB -b $((16 * $MB))"
app_args="$io_pattern $io_sizes"
unify_test_read $runmode "$app_args"

# read-gotcha -p n1 -n 32 -c 1MB -b 16MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-posix -p n1 -n 32 -c 1MB -b 16MB
runmode=posix
unify_test_read_posix "$app_args"

# Switch to -p nn
io_pattern="-p nn"
app_args="$io_pattern $io_sizes"

# read-posix -p n1 -n 32 -c 1MB -b 16MB
unify_test_read_posix "$app_args"

# read-gotcha -p n1 -n 32 -c 1MB -b 16MB
runmode=gotcha
unify_test_read $runmode "$app_args"

# read-static -p n1 -n 32 -c 1MB -b 16MB
runmode=static
unify_test_read $runmode "$app_args"
