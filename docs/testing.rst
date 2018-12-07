*************
Testing Guide
*************

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

Running the Examples
--------------------

The UnifyCR examples_ are also being used as integration tests with
continuation integration tools such as Bamboo_ or GitLab_.

To run any of these examples manually, refer to the :doc:`examples`
documentation.

.. add information on running all of these when the process is developed

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
    - Add this file to `t/Makefile.am`_ in the ``TESTS``, ``check_SCRIPTS``,
      and ``libexec_PROGRAMS`` variables

    You can then create the test suite file and any tests to be run in this
    suite.

    - Create a <test_suite_name>.c file (i.e., *sysio_suite.c*) that will
      contain the main function and mpi job that drives your suite

      - Mount unifycr from this file
      - Call testing functions (created in other files) in the order desired
        for testing, passing the mount point to those functions
    - Create a <test_suite_name>.h file that declares the names of all the test
      functions to be run by this suite and ``include`` this in the
      <test_suite_name>.c file
    - Create <test_name>.c files (i.e., *open.c*) that contains the testing
      function (i.e., ``open_test(char* unifycr_root)``) that houses the
      variables and TAP tests needed to test that individual function

      - Add the function name to the <test_suite_name>.h file
      - Call the function from the <test_suite_name>.c file

    The source files and flags for the test suite are then added to the bottom
    of `t/Makefile.am`_.

    - Add the <test_suite_name>.c and <test_suite_name>.h files to the
      ``<test_suite>_SOURCES`` variable
    - Add additional <test_name>.c files to the ``<test_suite>_SOURCES``
      variable as they are created
    - Add the associated flags for the test suite

Test Cases
----------

For testing C code, test cases are written using the `libtap library`_. See the
:ref:`C Program Tests <C-tests-label>` section below on how to write these
tests.

To add new test cases to any existing suite of tests:

    1. Simply add the desired tests (order matters) to the appropriate
       <test_name>.c file

If the test cases needing to be written don't already have a file they belong
in (i.e., testing a wrapper that doesn't have any tests yet):

    1. Creata a <function_name>.c file with a function called
       <function_name>_test(char* unifycr_root) that contains the desired TAP
       test cases
    2. Add the <function_name>_test to the corresponding <test_suite_name>.h
       file
    3. Add the <function_name>.c file to the bottom of `t/Makefile.am`_ under
       the appropriate ``<test_suite>_SOURCES`` variable
    4. The <function_name>_test function can now be called from the
       <test_suite_name>.c file

------------

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
libtap library calls ``tap_todo()`` and ``tap_end_todo()``.

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

        tap_todo(0, "Prove this someday");
        result = strcmp("P", "NP");
        ok(result == 0, "P equals NP: %d", result);
        tap_end_todo();

        done_testing();

        return 0;
    }

.. Integration Tests/Examples
   --------------------------

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
