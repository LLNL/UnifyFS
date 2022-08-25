#!/bin/bash

# This contains all the tests for testing the producer-consumer workflow using
# the write and read examples.
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


test_description="Producer-Consumer Tests"

PRODUCER_CONSUMER_USAGE="$(cat <<EOF
usage ./300-producer-consumer-tests.sh [options]

  options:
    -h, --help        print this (along with overall) help message
    -l, --laminate    laminate files between writing and reading
    -M, --mpiio       use MPI-IO instead of POSIX I/O

Run a series of producer-consumer workload tests using the UnifyFS write and
read example applications. By default, a series of different file sizes are
tested using POSIX-I/O on both a shared file and a file per process. They are
run multiple times for each mode the app was linked with (i.e., static, gotcha).

Providing available options can change the default I/O behavior and/or I/O type
used. The varying I/O types are mutually exclusive options and thus only one
should be provided at a time.

Only run this suite when using two or more hosts. Use 110-write-tests.sh
followed by 120-read-tests.sh when using a single host as it will be equivalent
in this case.

For more information on manually running tests, run './001-setup.sh -h'.
EOF
)"

for arg in "$@"
do
    case $arg in
        -h|--help)
            echo "$PRODUCER_CONSUMER_USAGE"
            ci_dir=$(dirname "$(readlink -fm $BASH_SOURCE)")
            exit
            ;;
        -l|--laminate)
            producer_consumer_laminate=yes
            ;;
        -M|--mpiio)
            [ -n "$write_io_type" ] &&
                { echo "ERROR: mutually exclusive options provided"; \
                  echo "$PRODUCER_CONSUMER_USAGE"; exit 2; } ||
                write_io_type="-M"
            ;;
        *)
            echo "$PRODUCER_CONSUMER_USAGE"
            exit 1
            ;;
    esac
done

if ! test_have_prereq MULTIPLE_HOSTS; then
    say "WARNING: Only run 300-producer-consumer-tests.sh suite with two or" \
        "more hosts."
    say "On a single host, 110-write-tests.sh followed by 120-read-tests.sh" \
        "is an equivalent set of tests."
    return
fi

# Call unify_run_test with the app name (use "producer" instead of "write" and
# "consumer" instead of "read"), mode, and arguments in order to automatically
# generate the MPI launch command to run the application and put the result in
# $app_output.
# Then evaluate the return code and output.
unify_test_producer_consumer() {
    app_name=producer-${1}

    # Run the producer test and get output
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    # Test the return code and resulting line count to determine pass/fail.
    test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
        test $rc = 0 &&
        test $lcount = 17
    '

    app_name=consumer-${1}

    # Run the consumer test and get output
    unify_run_test $app_name "$2" app_output
    rc=$?
    lcount=$(echo "$app_output" | wc -l)

    # Test the return code and resulting line count to determine pass/fail.
    test_expect_success "$app_name $2: (line_count=${lcount}, rc=$rc)" '
        test $rc = 0 &&
        test $lcount = 13
    '
}

### Run the producer-consumer tests ###

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

# Reset additional behavior to default
behavior=""

# Laminate after writing all data
if [ -n "$producer_consumer_laminate" ]; then
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
            unify_test_producer_consumer $mode "$app_args"
        done
    done
done

unset producer_consumer_laminate
unset write_io_type
