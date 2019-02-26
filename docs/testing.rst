*************
Testing Guide
*************

Implementing Tests
==================

We can never have enough testing. Any additional tests you can write are always
greatly appreciated.

Shell Script Tests
------------------

Test cases in shell scripts are implemented with sharness_, which is included
in the UnifyCR source distribution. See the file sharness.sh_ for all available
test interfaces. UnifyCR-specific sharness code is implemented in scripts in
the directory sharness.d_. Scripts in sharness.d_ are primarily used to set
environment variables and define convenience functions. All scripts in
sharness.d_ are automatically included when your script sources sharness.sh_.

The most common way to implement a test case with sharness is to use the
``test_expect_success()`` function. Your script must first set a test
description and source the sharness library. After all tests are defined, your
script should call ``test_done()`` to print a summary of the test run.

Test cases that demonstrate known breakage should use the sharness function
``test_expect_failure()`` to alert developers about the problem without
causing the overall test suite to fail. Failing test cases should be tracked
with github issues.

Here is an example of a sharness test:

.. code-block:: Bash
    :linenos:

    #!/bin/sh

    test_description="My awesome test cases"

    . $(dirname $0)/sharness.sh

    test_expect_success "Verify some critical invariant" '
        test 1 -eq 1
    '

    test_expect_failure "Prove this someday" '
        test "P" == "NP"
    '

    test_done

.. _C-tests-label:

C Program Tests
---------------

C programs use the `libtap library`_ to implement test cases. Convenience
functions common to test cases written in C are implemented in the library
`lib/testutil.c`_. If your C program needs to use environment variables set by
sharness, it can be wrapped in a shell script that first sources
`sharness.d/00-test-env.sh`_ and `sharness.d/01-unifycr-settings.sh`_. Your
wrapper shouldn't normally source sharness.sh_ itself because the TAP output
from sharness might conflict with that from libtap.

The most common way to implement a test with libtap is to use the ``ok()``
function. TODO test cases that demonstrate known breakage are surrounded by the
libtap library calls ``todo()`` and ``end_todo()``.

Here is an example libtap test:

.. code-block:: C
    :linenos:

    #include "t/lib/tap.h"
    #include <string.h>

    int main(int argc, char *argv[])
    {
        int result;

        result = (1 == 1);
        ok(result, "1 equals 1: %d", result);

        todo("Prove this someday");
        result = strcmp("P", "NP");
        ok(result == 0, "P equals NP: %d", result);
        end_todo;

        done_testing();

        return 0;
    }

------------

Adding Tests
============

The UnifyCR Test Suite uses the `Test Anything Protocol`_ (TAP) and the
Automake test harness. By convention, test scripts and programs that output
TAP are named with a ".t" extension.

To add a new test case to the test harness, follow the existing examples in
`t/Makefile.am`_. In short, add your test program to the list of tests in the
``TESTS`` variable. If it is a shell script, also add it to ``check_SCRIPTS``
so that it gets included in the source distribution tarball.

Test Suites
-----------

If multiple tests fit within the same category (i.e., tests for creat and mkdir
both fall under tests for sysio) then create a test suite to run those tests.
This makes it so less duplication of files and code is needed in order to create
additional tests.

To create a new test suite, look at how it is currently done for the
sysio_suite in `t/Makefile.am`_ and `t/sys/sysio_suite.c`_:

    If you're testing C code, you'll need to use environment variables set by
    sharness.

    - Create a shell script, *<####-suite-name>.t* (the #### indicates the
      order in which they should be run by the tap-driver), that wraps your
      suite and sources `sharness.d/00-test-env.sh`_ and
      `sharness.d/01-unifycr-settings.sh`_
    - Add this file to `t/Makefile.am`_ in the ``TESTS`` and ``check_SCRIPTS``
      variables and add the name of the file (but with a .t extension) this
      script runs to the ``libexec_PROGRAMS`` variable

    You can then create the test suite file and any tests to be run in this
    suite.

    - Create a <test_suite_name>.c file (i.e., *sysio_suite.c*) that will
      contain the main function and mpi job that drives your suite

      - Mount unifycr from this file
      - Call testing functions that contain the test cases
        (created in other files) in the order desired for testing, passing the
        mount point to those functions
    - Create a <test_suite_name>.h file that declares the names of all the test
      functions to be run by this suite and ``include`` this in the
      <test_suite_name>.c file
    - Create <test_name>.c files (i.e., *open.c*) that contains the testing
      function (i.e., ``open_test(char* unifycr_root)``) that houses the
      variables and libtap tests needed to test that individual function

      - Add the function name to the <test_suite_name>.h file
      - Call the function from the <test_suite_name>.c file

    The source files and flags for the test suite are then added to the bottom
    of `t/Makefile.am`_.

    - Add the <test_suite_name>.c and <test_suite_name>.h files to the
      ``<test_suite>_SOURCES`` variable
    - Add additional <test_name>.c files to the ``<test_suite>_SOURCES``
      variable as they are created
    - Add the associated flags for the test suite (if the suite is for testing
      wrappers, add a suite and flags for both a gotcha and a static build)

Test Cases
----------

For testing C code, test cases are written using the `libtap library`_. See the
:ref:`C Program Tests <C-tests-label>` section above on how to write these
tests.

To add new test cases to any existing suite of tests:

    1. Simply add the desired tests (order matters) to the appropriate
       <test_name>.c file

If the test cases needing to be written don't already have a file they belong
in (i.e., testing a wrapper that doesn't have any tests yet):

    1. Creata a <function_name>.c file with a function called
       <function_name>_test(char* unifycr_root) that contains the desired
       libtap test cases
    2. Add the <function_name>_test to the corresponding <test_suite_name>.h
       file
    3. Add the <function_name>.c file to the bottom of `t/Makefile.am`_ under
       the appropriate ``<test_suite>_SOURCES`` variable(s)
    4. The <function_name>_test function can now be called from the
       <test_suite_name>.c file

------------

Running the Tests
=================

To manually run the UnifyCR test suite, simply run ``make check`` from your
build/t directory. If changes are made to existing files in the test suite, the
tests can be run again by simply doing ``make clean`` followed by ``make
check``. Individual tests may be run by hand. The test ``0001-setup.t`` should
normally be run first to start the UnifyCR daemon.

.. note::

    If you are using Spack to install UnifyCR then there are two ways to
    manually run these tests:

    1. Upon your installation with Spack

        ``spack install -v --test=root unifycr``

    2. Manually from Spack's build directory

        ``spack install --keep-stage unifycr``

        ``spack cd unifycr``

        ``cd spack-build/t``

        ``make check``

The tests in https://github.com/LLNL/UnifyCR/tree/dev/t are run automatically
by `Travis CI`_ along with the :ref:`style checks <style-check-label>` when a
pull request is created or updated. All pull requests must pass these tests
before they will be accepted.

Interpreting the Results
------------------------

.. sidebar:: TAP Output

    .. image:: images/tap-output.png
        :align: center

After a test runs, its result is printed out consisting of its status followed
by its description and potentially a TODO/SKIP message. Once all the tests
have completed (either from being run manually or by `Travis CI`_), the overall
results are printed out, as shown in the image on the right.


There are six possibilities for the status of each test: PASS, FAIL, XFAIL,
XPASS, SKIP, and ERROR.

PASS
    The test had the desired result.
FAIL
    The test did not have the desired result. These must be fixed before any
    code changes can be accepted.

    If a FAIL occurred after code had been added/changed then most likely a bug
    was introduced that caused the test to fail. Some tests may fail as a
    result of earlier tests failing. Fix bugs that are causing earlier tests
    to fail first as, once they start passing, subsequent tests are likely to
    start passing again as well.
XFAIL
    The test was expected to fail, and it did fail.

    An XFAIL is created by surrounding a test with ``todo()`` and ``end_todo``.
    These are tests that have identified a bug that was already in the code,
    but the cause of the bug hasn't been found/resolved yet. An optional
    message can be passed to the ``todo("message")`` call which will be printed
    after the test has run. Use this to explain how the test should behave or
    any thoughts on why it might be failing. An XFAIL is not meant to be used
    to make a failing test start "passing" if a bug was introduced by code
    changes.
XPASS
    A test passed that was expected to fail. These must be fixed before any
    code changes can be accepted.

    The relationship of an XPASS to an XFAIL is the same as that of a FAIL to a
    PASS. An XPASS will typically occur when a bug causing an XFAIL has been
    fixed and the test has started passing. If this is the case, remove the
    surrounding ``todo()`` and ``end_todo`` from the failing test.
SKIP
    The test was skipped.

    Tests are skipped because what they are testing hasn't been implemented
    yet, or they apply to a feature/variant that wasn't included in the build
    (i.e., HDF5). A SKIP is created by surrounding the test(s) with
    ``skip(test, n, message)`` and ``end_skip`` where the ``test`` is what
    determines if these tests should be skipped and ``n`` is the number of
    subsequent tests to skip. Remove these if it is no longer desired for those
    tests to be skipped.
ERROR
    A test or test suite exited with a non-zero status.

    When a test fails, the containing test suite will exit with a non-zero
    status, causing an ERROR. Fixing any test failures should resolve the
    ERROR.

Running the Examples
--------------------

The UnifyCR examples_ are also being used as integration tests with
continuation integration tools such as Bamboo_ or GitLab_.

To run any of these examples manually, refer to the :doc:`examples`
documentation.

.. explicit external hyperlink targets

.. _Bamboo: https://www.atlassian.com/software/bamboo
.. _GitLab: https://about.gitlab.com
.. _examples: https://github.com/LLNL/UnifyCR/tree/dev/examples/src
.. _libtap library: https://github.com/zorgnax/libtap
.. _lib/testutil.c: https://github.com/LLNL/UnifyCR/blob/dev/t/lib/testutil.c
.. _t/Makefile.am: https://github.com/LLNL/UnifyCR/blob/dev/t/Makefile.am
.. _t/sys/sysio_suite.c: https://github.com/LLNL/UnifyCR/blob/dev/t/sys/sysio_suite.c
.. _Test Anything Protocol: https://testanything.org
.. _Travis CI: https://docs.travis-ci.com
.. _sharness: https://github.com/chriscool/sharness
.. _sharness.d: https://github.com/LLNL/UnifyCR/tree/dev/t/sharness.d
.. _sharness.d/00-test-env.sh: https://github.com/LLNL/UnifyCR/blob/dev/t/sharness.d/00-test-env.sh
.. _sharness.d/01-unifycr-settings.sh: https://github.com/LLNL/UnifyCR/blob/dev/t/sharness.d/01-unifycr-settings.sh
.. _sharness.sh: https://github.com/LLNL/UnifyCR/blob/dev/t/sharness.sh
