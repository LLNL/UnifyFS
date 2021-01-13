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


test_description="Read Tests"

while [[ $# -gt 0 ]]
do
    case $1 in
        -h|--help)
            echo "usage ./120-read-tests.sh -h|--help"
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            $ci_dir/001-setup.sh -h
            exit
            ;;
        *)
            echo "usage ./120-read-tests.sh -h|--help"
            exit 1
            ;;
    esac
done

# Call unify_run_test with the app name, mode, and arguments in order to
# automatically generate the MPI launch command to run the application and put
# the result in $app_output.
# Then evaluate the return code and output.
unify_test_read() {
    app_name=read-${1}

    # Run the test and get output
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    # Test the return code and resulting line count to determine pass/fail.
    test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
        test $rc = 0 &&
        test $lcount = 14
    '
}

### Run the read tests ###

# Array to determine the different file sizes that are created and tested. Each
# item contains the number of blocks, chunk size, and block size to use when
# writing or reading the file.
io_sizes=("-n 32 -c $((64 * $KB)) -b $MB"
          "-n 64 -c $MB -b $((4 *  $MB))"
          "-n 32 -c $((4 * $MB)) -b $((16 * $MB))"
)

# I/O patterns to test with.
# Includes shared file (-p n1) and file-per-process (-p nn)
io_patterns=("-p n1" "-p nn")

# Mode of each test, whether static, gotcha, or posix (if desired)
modes=(static gotcha)

# To run posix tests, set UNIFYFS_CI_TEST_POSIX=yes
if test_have_prereq POSIX; then
    modes+=(posix)
fi

# For each io_size, test with each io_pattern and for each io_pattern, test each
# mode
for io_size in "${io_sizes[@]}"; do
    for io_pattern in "${io_patterns[@]}"; do
        app_args="$io_pattern $io_size"
        for mode in "${modes[@]}"; do
            unify_test_read $mode "$app_args"
        done
    done
done
