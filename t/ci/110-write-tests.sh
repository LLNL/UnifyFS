#!/bin/bash

# This contains all the tests for the write example.
#
# There are convenience functions such as `unify_run_test()` and get_filename()
# in the ci-functions.sh script that can make adding new tests easier. See the
# full UnifyFS documentatation for more info.
#
# There are multiple ways to run an example using `unify_run_test()`
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
# For these tests, always include -b, -c, -n, and -p in the app_args


test_description="Write Tests"

WRITE_USAGE="$(cat <<EOF
usage ./110-write-tests.sh [options]

  options:
    -h, --help        print this (along with overall) help message
    -l, --laminate    laminate after finished writing
    -M, --mpiio       use MPI-IO instead of POSIX I/O

Run a series of tests on the UnifyFS write example application. By default, a
series of different file sizes are tested using POSIX-I/O on both a shared
file and a file per process. They are run multiple times for each mode the app
was built with (static, gotcha, and optionally posix).

Providing available options can change the default I/O behavior and/or I/O type
used. The varying I/O types are mutually exclusive options and thus only one
should be provided at a time.

For more information on manually running tests, run './001-setup.sh -h'.
EOF
)"

for arg in "$@"
do
    case $arg in
        -h|--help)
            echo "$WRITE_USAGE"
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            exit
            ;;
        -l|--laminate)
            write_laminate=yes
            ;;
        -M|--mpiio)
            [ -n "$write_io_type" ] &&
                { echo "ERROR: mutually exclusive options provided"; \
                  echo "$WRITE_USAGE"; exit 2; } ||
                write_io_type="-M"
            ;;
        *)
            echo "$WRITE_USAGE"
            exit 1
            ;;
    esac
done

# Call unify_run_test with the app name, mode, and arguments in order to
# automatically generate the MPI launch command to run the application and put
# the result in $app_output.
# Then evaluate the return code and output.
unify_test_write() {
    app_name=write-${1}

    # Run the test and get output
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    # Test the return code and resulting line count to determine pass/fail.
    # If mode is posix, also test that the file or files exist at the mountpoint
    # depending on whether testing shared file or file-per-process.
    if [ "$1" = "posix" ]; then
        filename=$(get_filename $app_name "$2" ".app")

        test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
            test $rc = 0 &&
            test $lcount = 17 &&
            if [[ $io_pattern =~ (n1)$ ]]; then
                test_path_is_file ${UNIFYFS_CI_POSIX_MP}/$filename
            else
                test_path_has_file_per_process $UNIFYFS_CI_POSIX_MP $filename
            fi
        '
    else
        test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
            test $rc = 0 &&
            test $lcount = 17
        '
    fi
}

### Run the write tests ###

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
modes=(gotcha)

# static linker wrapping will not see the syscalls in the MPI-IO libraries
if [ "$write_io_type" != "-M" ]; then
    modes+=(static)
fi

# To run posix tests, set UNIFYFS_CI_TEST_POSIX=yes
if test_have_prereq POSIX; then
    modes+=(posix)
fi

# Reset additional behavior to default
behavior=""

# Laminate after writing all data
if [ -n "$write_laminate" ]; then
    behavior="$behavior -l"
fi

# Set I/O type
if [ -n "$write_io_type" ]; then
    behavior="$behavior $write_io_type"
fi

# For each io_size, test with each io_pattern and for each io_pattern, test each
# mode
for io_size in "${io_sizes[@]}"; do
    for io_pattern in "${io_patterns[@]}"; do
        app_args="$io_pattern $io_size $behavior"
        for mode in "${modes[@]}"; do
            unify_test_write $mode "$app_args"
        done
    done
done

unset write_io_type
unset write_laminate
