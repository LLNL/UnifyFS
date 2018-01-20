## Running the UnifyCR Test Suite

To run the UnifyCR test suite, simply run `make check`.
Individual tests may be run by hand. The test `0001-setup.t` should
normally be run first to start the UnifyCR daemon.

## Adding Test Cases

The UnifyCR Test Suite uses the [Test Anything
Protocol](https://testanything.org/) (TAP) and the Automake test
harness. By convention, test scripts and programs that output TAP are
named with a ".t" extension.

To add a new test case to the test harness, follow the existing examples
in `t/Makefile.am`.  In short, add your test program to the list of
tests in the `TESTS` variable. If it is a shell script, also add it to
`check_SCRIPTS` so that it gets included in the source distribution
tarball.

## Implementing shell script tests

Test cases in shell scripts are implemented with
[sharness](https://github.com/chriscool/sharness), which is included in
the UnifyCR source distribution. See the file `sharness.sh` for all
available test interfaces. UnifyCR-specific sharness code is implemented
in scripts in the directory `sharness.d`. Scripts in `sharness.d` are
primarily used to set environment variables and define convenience
functions.  All scripts in `sharness.d` are automatically included when
your script sources `sharness.sh`.

The most common way to implement a test case with sharness is to use
the `test_expect_success()` function. Your script must first set a test
description and source the sharness library. After all tests are defined
your script should call `test_done()` to print a summary of the test run.

Test cases that demonstrate known breakage should use the sharness
function `test_expect_failure()` to alert developers about the problem
without causing the overall test suite to fail. Failing test cases
should be tracked with github issues.

Here is an example sharness test.

    #!/bin/sh

    test_description='My awesome test cases'

    . $(dirname "$0)/sharness.sh

    test_expect_success 'Verify some critical invariant' '
        test 1 -eq 1
    '

    test_expect_failure 'Prove this someday' '
        test "P" == "NP"
    '

    test_done

## Implementing C program tests

C programs use the [libtap library](https://github.com/zorgnax/libtap)
to implement test cases. Convenience functions common to test cases
written in C are implemmented in the library `lib/testutil.c`. If your C
program needs to use environment variables set by sharness, it can be
wrapped in a shell script that first sources `sharness.d/00-test-env.sh`
and `sharenss.d/01-unifycr-settings.sh`.  Your wrapper shouldn't
normally source `sharness.sh` itself because the TAP output from
sharness might conflict with that from libtap.

The most common way to implement a test with libtap is to use the `ok()`
function.  TODO test cases that demonstrate known breakage are
surrounded by the libtap library calls `tap_todo()` and
`tap_end_todo()`.

Here is an example libtap test.

    #include "t/lib/tap.h"
    #include <string.h>

    int main(int argc, char *argv[])
    {
        int result;

        result = (1 == 1);
        ok(result, "1 equals 1: %d", result);

        tap_todo(0, "Prove this someday");
        result = strcmp("P", "NP");
        ok(result == 0, "P equals NP: %d", result);
        tap_end_todo();

        done_testing();

        return 0;
    }
