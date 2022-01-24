#!/bin/bash

# This contains all the tests for the writeread example.
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

test_description="Writeread Tests"

WRITEREAD_USAGE="$(cat <<EOF
usage ./100-writeread-tests.sh [options]

  options:
    -h, --help        print this (along with overall) help message
    -M, --mpiio       use MPI-IO instead of POSIX I/O
    -x, --shuffle     read different data than written

Run a series of tests on the UnifyFS writeread example application. By default,
a series of different file sizes are tested using POSIX-I/O on both a shared
file and a file per process. They are run multiple times for each mode the app
was built with (static, gotcha, and optionally posix).

Providing available options can change the default I/O behavior and/or I/O type
used. The varying I/O types are mutually exclusive options and thus only one
should be provided at a time.
EOF
)"

for arg in "$@"
do
    case $arg in
        -h|--help)
            echo "$WRITEREAD_USAGE"
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            $ci_dir/001-setup.sh -h
            exit
            ;;
        -l|--laminate)
            writeread_laminate=yes
            ;;
        -M|--mpiio)
            [ -n "$writeread_io_type" ] &&
                { echo "ERROR: mutually exclusive options provided"; \
                  echo "$WRITEREAD_USAGE"; exit 2; } ||
                writeread_io_type="-M"
            ;;
        -x|--shuffle)
            writeread_shuffle=yes
            ;;
        *)
            echo "$WRITEREAD_USAGE"
            exit 1
            ;;
    esac
done

# Call unify_run_test with the app name, mode, and arguments in order to
# automatically generate the MPI launch command to run the application and put
# the result in $app_output.
# Then evaluate the return code and output.
unify_test_writeread() {
    app_name=writeread-${1}

    # Run the test and get output.
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    if [ -n "$writeread_laminate" ]; then
        expected_lcount=29
    else
        expected_lcount=17
    fi

    # Test the return code and resulting line count to determine pass/fail.
    # If mode is posix, also test that the file or files exist at the mountpoint
    # depending on whether testing shared file or file-per-process.
    if [ "$1" = "posix" ]; then
        filename=$(get_filename $app_name "$2" ".app")

        test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
            test $rc = 0 &&
            test $lcount = $expected_lcount &&
            if [[ $io_pattern =~ (n1)$ ]]; then
                test_path_is_file ${UNIFYFS_CI_POSIX_MP}/$filename
            else
                test_path_has_file_per_process $UNIFYFS_CI_POSIX_MP $filename
            fi
        '
    else
        test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
            test $rc = 0 &&
            test $lcount = $expected_lcount
        '
    fi
}

### Run the writeread tests ###
# For these tests, always include -b, -c, -n, and -p in the app_args

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
if [ "$writeread_io_type" != "-M" ]; then
    modes+=(static)
fi

# To run posix tests, set UNIFYFS_CI_TEST_POSIX=yes
if test_have_prereq POSIX; then
    modes+=(posix)
fi

# Reset additional behavior to default
behavior=""

# Laminate after writing all data
if [ -n "$writeread_laminate" ]; then
    behavior="$behavior -l"
fi

# Set I/O type
if [ -n "$writeread_io_type" ]; then
    behavior="$behavior $writeread_io_type"
fi

# Read different data than written
if [ -n "$writeread_shuffle" ]; then
    behavior="$behavior -x"
fi

# For each io_size, test with each io_pattern and for each io_pattern, test each
# mode
for io_size in "${io_sizes[@]}"; do
    for io_pattern in "${io_patterns[@]}"; do
        app_args="$io_pattern $io_size $behavior"
        for mode in "${modes[@]}"; do
            unify_test_writeread $mode "$app_args"
        done
    done
done

unset writeread_io_type
unset writeread_laminate
unset writeread_shuffle
