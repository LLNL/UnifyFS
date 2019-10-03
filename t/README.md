# Testing

UnifyFS's unit testing and continuous integration testing suites.

Our [Testing Guide](https://unifyfs.readthedocs.io/en/dev/testing.html) has our
complete testing documentation.

## Unit Tests

The UnifyFS Unit Test Suite uses the Test Anything Protocol (TAP) and the
Automake test harness. By convention, test scripts and programs that output TAP
are named with a “.t” extension.

Test cases in shell scripts are implemented with
[sharness](https://github.com/chriscool/sharness), which is included in
the UnifyFS source distribution. See the file `sharness.sh` for all available
test interfaces. UnifyFS-specific sharness code is implemented in scripts in the
directory `sharness.d/`. Scripts in `sharness.d/` are primarily used to set
environment variables and define convenience functions.  All scripts in
`sharness.d/` are automatically included when your script sources `sharness.sh`.

C programs use the [libtap library](https://github.com/zorgnax/libtap)
to implement test cases. Convenience functions common to test cases written in
C are implemmented in the library `lib/testutil.c`.

## Continuous Integration Tests

The UnifyFS Continuous Integration (CI) Test Suite is found in `ci/` and also uses [sharness](https://github.com/chriscool/sharness).
Additional sharness convenience functions and variables needed for the CI tests
are also found in `ci/` and are sourced when running `ci/001-setup.sh`.
