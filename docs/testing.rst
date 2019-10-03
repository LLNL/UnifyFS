=============
Testing Guide
=============

We can never have enough testing. Any additional tests you can write are always
greatly appreciated.

----------
Unit Tests
----------

Implementing Tests
******************

The UnifyFS Test Suite uses the `Test Anything Protocol`_ (TAP) and the
Automake test harness. This test suite has two types of TAP tests (shell scripts
and C) to allow for testing multiple aspects of UnifyFS.

Shell Script Tests
^^^^^^^^^^^^^^^^^^

Test cases in shell scripts are implemented with sharness_, which is included
in the UnifyFS source distribution. See the file sharness.sh_ for all available
test interfaces. UnifyFS-specific sharness code is implemented in scripts in
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
^^^^^^^^^^^^^^^

C programs use the `libtap library`_ to implement test cases. Convenience
functions common to test cases written in C are implemented in the library
`lib/testutil.c`_. If your C program needs to use environment variables set by
sharness, it can be wrapped in a shell script that first sources
`sharness.d/00-test-env.sh`_ and `sharness.d/01-unifyfs-settings.sh`_. Your
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
************

The UnifyFS Test Suite uses the `Test Anything Protocol`_ (TAP) and the
Automake test harness. By convention, test scripts and programs that output
TAP are named with a ".t" extension.

To add a new test case to the test harness, follow the existing examples in
`t/Makefile.am`_. In short, add your test program to the list of tests in the
``TESTS`` variable. If it is a shell script, also add it to ``check_SCRIPTS``
so that it gets included in the source distribution tarball.

Test Suites
^^^^^^^^^^^

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
      `sharness.d/01-unifyfs-settings.sh`_
    - Add this file to `t/Makefile.am`_ in the ``TESTS`` and ``check_SCRIPTS``
      variables and add the name of the file (but with a .t extension) this
      script runs to the ``libexec_PROGRAMS`` variable

    You can then create the test suite file and any tests to be run in this
    suite.

    - Create a <test_suite_name>.c file (i.e., *sysio_suite.c*) that will
      contain the main function and mpi job that drives your suite

      - Mount unifyfs from this file
      - Call testing functions that contain the test cases
        (created in other files) in the order desired for testing, passing the
        mount point to those functions
    - Create a <test_suite_name>.h file that declares the names of all the test
      functions to be run by this suite and ``include`` this in the
      <test_suite_name>.c file
    - Create <test_name>.c files (i.e., *open.c*) that contains the testing
      function (i.e., ``open_test(char* unifyfs_root)``) that houses the
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
^^^^^^^^^^

For testing C code, test cases are written using the `libtap library`_. See the
:ref:`C Program Tests <C-tests-label>` section above on how to write these
tests.

To add new test cases to any existing suite of tests:

    1. Simply add the desired tests (order matters) to the appropriate
       <test_name>.c file

If the test cases needing to be written don't already have a file they belong
in (i.e., testing a wrapper that doesn't have any tests yet):

    1. Creata a <function_name>.c file with a function called
       <function_name>_test(char* unifyfs_root) that contains the desired
       libtap test cases
    2. Add the <function_name>_test to the corresponding <test_suite_name>.h
       file
    3. Add the <function_name>.c file to the bottom of `t/Makefile.am`_ under
       the appropriate ``<test_suite>_SOURCES`` variable(s)
    4. The <function_name>_test function can now be called from the
       <test_suite_name>.c file

------------

Running the Tests
*****************

To manually run the UnifyFS test suite, simply run ``make check`` from your
build/t directory. If changes are made to existing files in the test suite, the
tests can be run again by simply doing ``make clean`` followed by ``make
check``. Individual tests may be run by hand. The test ``0001-setup.t`` should
normally be run first to start the UnifyFS daemon.

.. note::

    If you are using Spack to install UnifyFS then there are two ways to
    manually run these tests:

    1. Upon your installation with Spack

        ``spack install -v --test=root unifyfs``

    2. Manually from Spack's build directory

        ``spack install --keep-stage unifyfs``

        ``spack cd unifyfs``

        ``cd spack-build/t``

        ``make check``

The tests in https://github.com/LLNL/UnifyFS/tree/dev/t are run automatically
by `Travis CI`_ along with the :ref:`style checks <style-check-label>` when a
pull request is created or updated. All pull requests must pass these tests
before they will be accepted.

Interpreting the Results
^^^^^^^^^^^^^^^^^^^^^^^^

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
^^^^^^^^^^^^^^^^^^^^

To run any of these examples manually, refer to the :doc:`examples`
documentation.

The UnifyFS examples_ are also being used as integration tests with
continuation integration tools such as Bamboo_ or GitLab_.

------------

-----------------
Integration Tests
-----------------

The UnifyFS examples_ are being used as integration tests with continuation
integration tools such as Bamboo_ or GitLab_.

To run any of these examples manually, refer to the :doc:`examples`
documentation.

------------

Running the Tests
*****************

.. attention::

    UnifyFS's integration test suite requires MPI and currently only supports
    ``srun`` and ``jsrun`` MPI launch commands. Changes are coming to support
    ``mpirun``.

UnifyFS's integration tests are primarly set up to be run all as one suite.
However, they can be run individually if desired.

The testing scripts in `t/ci`_ depend on sharness_, which is set up in the
containing *t/* directory. These tests will not function properly if moved or if
they cannot find the sharness files.

.. important::

    Whether running all tests or individual tests, first make sure you have
    either interactively allocated nodes or are submitting a batch job to run
    them.

    Also make sure all :ref:`dependencies <spack-build-label>` are installed and
    loaded.

By default, the integration tests will use the number of processes-per-node as
there are nodes allocated for the job (i.e., if 4 nodes were allocated, then 4
processes will be run per node). This can be changed by setting the
:ref:`$CI_NPROCS <ci-nprocs-label>` environment variable.

.. note::

    In order to run the the integration tests from a Spack_ installation of
    UnifyFS, you'll need to tell Spack to use a different location for staging
    builds in order to have the source files available from inside an allocation.

    Open your Spack config file

        ``spack config edit config``

    and provide a path that is visible during job allocations:

        .. code-block:: yaml

            config:
              build_stage:
              - /visible/path/from/all/allocated/nodes
              # or build directly inside Spack's install directory
              - $spack/var/spack/stage

    Then make sure to include the ``--keep-stage`` option when installing:

        ``spack install --keep-stage unifyfs``

Running All Tests
^^^^^^^^^^^^^^^^^

To run all of the tests, simply run ``./RUN_CI_TESTS.sh``.

.. code-block:: BASH

    $ ./RUN_CI_TESTS.sh

or

.. code-block:: BASH

    $ prove -v RUN_CI_TESTS.sh

Running Individual Tests
^^^^^^^^^^^^^^^^^^^^^^^^

In order to run individual tests, testing functions and variables need to be set
up first, and the UnifyFS server needs to be started. To do this, first source
the *t/ci/001-setup.sh* script followed by *002-start-server.sh*. Then source
each desired test script after that preceded by ``$CI_DIR/``. When finished,
source the *990-stop-server.sh* script last to stop the server and clean up.

.. code-block:: BASH

    $ . full/path/to/001-setup.sh
    $ . $CI_DIR/002-start-server.sh
    $ . $CI_DIR/100-writeread-tests.sh
    $ . $CI_DIR/990-stop-server.sh

Configuration Variables
^^^^^^^^^^^^^^^^^^^^^^^

Along with the already provided :doc:`configuration` options/environment
variables, there are available environment variables used by the integration
testing suite that can be set in order to change the default behavior. They are
listed below in the order they are set up.

``CI_PROJDIR``
""""""""""""""

USAGE: ``CI_PROJDIR=/base/location/to/search/for/UnifyFS/source/files``

During setup, the integration tests will search for the ``unifyfsd`` executable
and installed example scripts if the UnifyFS install directory is not provided by
the user with the ``UNIFYFS_INSTALL`` envar. ``CI_PROJDIR`` is the base location
where this search will start and defaults to ``CI_PROJDIR=$HOME``.


``UNIFYFS_INSTALL``
"""""""""""""""""""

USAGE: ``UNIFYFS_INSTALL=/path/to/dir/containing/UnifyFS/bin/directory``

The full path to the directory containing the *bin/* and *libexec/* directories
for a UnifyFS installation. Set this envar to prevent the integration tests from
searching for a UnifyFS install directory automatically.

.. _ci-nprocs-label:

``CI_NPROCS``
"""""""""""""

USAGE: ``CI_NPROCS=<number-of-process-per-node>``

The number of processes to use per node inside a job allocation. This defaults
to the number of processes per node as there are nodes in the allocation (i.e.,
if 4 nodes were allocated, then 4 processes will be run per node). This should
be adjusted if fewer processes are desired on multiple nodes, multiple processes
are desired on a single node, or a large number of nodes have been allocated.

``CI_LOG_CLEANUP``
""""""""""""""""""

USAGE: ``CI_LOG_CLEANUP=yes|YES|no|NO``

In the event ``$UNIFYFS_LOG_DIR`` has **not** been set, the logs will be put in
``$SHARNESS_TRASH_DIRECTORY``, as set up by sharness.sh_, and cleaned up
automatically after the tests have run. The logs will be in a
*<system-name>_<jobid>/* subdirectory. Should any tests fail, the trash
directory will not be cleaned up for debugging purposes. Setting
``CI_LOG_CLEANUP=no|NO`` will move the *<system-name>_<jobid>/* logs directory
to ``$CI_DIR`` (the directory containing the integration tests) to
allow them to persist even when all tests pass. This envar defauls to ``yes``.

.. note::

    Setting ``$UNIFYFS_LOG_DIR`` will put all created logs in the designated path
    and will not clean them up.

``CI_HOST_CLEANUP``
"""""""""""""""""""

USAGE: ``CI_HOST_CLEANUP=yes|YES|no|NO``

After all tests have run, the nodes on which the tests were ran will
automatically be cleaned up. This cleanup includes ensuring ``unifyfsd`` has
stopped and deleting any files created by UnifyFS or its dependencies. Set
``CI_HOST_CLEANUP=no|NO`` to skip cleaning up. This envar defaults to ``yes``.

.. note::

    PDSH_ is required for cleanup and cleaning up is simply skipped if not
    found.

``CI_CLEANUP``
""""""""""""""

USAGE: ``CI_CLEANUP=yes|YES|no|NO``

Setting this to ``no|NO`` sets both ``$CI_LOG_CLEANUP`` and ``$CI_HOST_CLEANUP``
to ``no|NO``.

``CI_TEMP_DIR``
""""""""""""""""

USAGE: ``CI_TEMP_DIR=/path/for/temporary/files/created/by/UnifyFS``

Can be used as a shortcut to set ``UNIFYFS_RUNSTATE_DIR`` and
``UNIFYFS_META_DB_PATH`` to the same path.  This envar defaults to
``CI_TEMP_DIR=${TMPDIR}/unifyfs.${USER}.${JOB_ID}``.

``CI_STORAGE_DIR``
"""""""""""""""""""

USAGE: ``CI_STORAGE_DIR=/path/for/storage/files/``

Can be used as a shortcut to set ``UNIFYFS_SPILLOVER_DATA_DIR`` and
``UNIFYFS_SPILLOVER_META_DIR`` to the same path.  This envar defaults to
``CI_STORAGE_DIR=${TMPDIR}/unifyfs.${USER}.${JOB_ID}``.

``CI_TEST_POSIX``
"""""""""""""""""

USAGE: ``CI_TEST_POSIX=yes|YES|no|NO``

Determines whether any ``<example-name>-posix`` tests should be run since they
require a real mountpoint to exist.

This envar defaults to ``yes``. However, when ``$UNIFYFS_MOUNTPOINT`` is set to a
real directory, this envar is switched to ``no``. The idea behind this is that
the tests can be run a first time with a fake mountpoint (which will also run
the posix tests), and then the tests can be run again with a real mountpoint and
the posix tests wont be run twice. This behavior can be overridden by setting
``CI_TEST_POSIX=yes|YES`` before running the integration tests when
``$UNIFYFS_MOUNTPOINT`` is set to an existing directory.

An example of testing a posix example can be see :ref:`below <posix-ex-label>`.

.. note::

    The the posix mountpoint envar, ``CI_POSIX_MP``, is set up inside
    ``$SHARNESS_TRASH_DIRECTORY`` automatically and cleaned up afterwards.
    However, this envar can be set before running the integration tests as well.
    If setting this, ensure that it is a shared file system that all allocated
    nodes can see.

------------

Adding New Tests
****************

In order to add additional tests, create a script after the fashion of
`t/ci/100-writeread-tests.sh`_ where the prefixed number indicates the desired
order for running the tests. Then source that script in `t/ci/RUN_CI_TESTS.sh`_
in the desired order.

Just like the helpers functions found in sharness.d_, there are continuous
integration helper functions (see :ref:`below <helper-label>` for more details)
available in `t/ci/ci-functions.sh`_. These exist to help make adding new tests
as simple as possible.

One particularly useful function is ``unify_run_test()``. Currently, this
function is set up to work for the *write*, *read*, *writeread*, and
*checkpoint-restart* examples. This function sets up the MPI job run command and
default arguments as well as any default arguments wanted by all examples. See
:ref:`below <unify-run-test-label>` for details.

.. _helper-label:

Example Helper Functions
^^^^^^^^^^^^^^^^^^^^^^^^

There are helper functions available in `t/ci/ci-functions.sh`_ that can make
running and testing the examples much easier. These may get adjusted over time
to accommodate other examples, or additional functions may need to be written.
Some of the main helper functions that might be useful for running examples are:

.. _unify-run-test-label:

``unify_run_test()``
""""""""""""""""""""

USAGE: ``unify_run_test app_name "app_args" [output_variable_name]``

Given a example application name and application args, this function runs the
example with the appropriate MPI runner and args. This function is meant to make
running the cr, write, read, and writeread examples as easy as possible.

The ``build_test_command()`` function is called by this function which
automatically sets any options that are always wanted (-vkf as well as -U and
the appropriate -m if posix test or not). The stderr output file is also created
(based on the filename that is autogenerated) and the appropriate option is set
for the MPI job run command.

Args that can be passed in are ([-pncbx][-A|-M|-P|-S|-V]). All other args (see
:ref:`Running the Examples <run-ex-label>`) are set automatically, including the
filename (which is generated based on the input ``$app_name`` and ``$app_args``).

The third parameter is an optional "pass-by-reference" parameter that can
contain the variable name for the resulting output to be stored in, allowing
this function to be used in one of two ways:

.. code-block:: BASH
    :caption: Using command substitution

    app_output=$(unify_run_test $app_name "$app_args")

or

.. code-block:: BASH
    :caption: Using a "pass-by-reference" variable

    unifyfs_run_test $app_name "$app_args" app_output

This function returns the return code of the executed example as well as the
output produced by running the example.

.. note::

    If ``unify_run_test()`` is simply called with only two arguments and without
    using command substitution, the resulting output will be sent to the standard
    output.

The results can then be tested with sharness_:

.. code-block:: BASH
    :emphasize-lines: 7,11-14

    basetest=writeread
    runmode=static

    app_name=${basetest}-${runmode}
    app_args="-p n1 -n32 -c $((16 * $KB)) -b $MB

    unify_run_test $app_name "$app_args" app_output
    rc=$?
    line_count=$(echo "$app_output" | wc -l)

    test_expect_success "$app_name $app_args: (line_count=$line_count, rc=$rc)" '
        test $rc = 0 &&
        test $line_count = 8
    '

``get_filename()``
""""""""""""""""""

USAGE: ``get_filename app_name app_args [app_suffix]``

Builds and returns the filename for an example so that if it shows up in the
``$UNIFYFS_MOUNTPOINT`` (when using an existing mountpoint), it can be tracked
to its originating test for debugging. Error files are created with this
filename and a ``.err`` suffix and placed in the logs directory for debugging.

Also allows testers to get what the filename will be in advance if called
from a test suite. This can be used for posix tests to ensure the file showed
up in the mount point, as well as for cp/stat tests that potentially need the
filename from a previous test.

Note that the filename created by ``unify_run_test()`` will have a ``.app``
suffix.

Returns a string with the spaces removed and hyphens replaced by underscores.

.. code-block:: BASH

    get_filename write-static "-p n1 -n 32 -c 1024 -b 1048576" ".app"
    write-static_pn1_n32_c1KB_b1MB.app

Some uses cases may be:

- posix tests where the file existence is checked for after a test was run
- cp/stat tests where an already existing filename from a prior test is needed

For example:

.. _posix-ex-label:

.. code-block:: BASH
    :emphasize-lines: 10,15

    basetest=writeread
    runmode=posix

    app_name=${basetest}-${runmode}
    app_args="-p nn -n32 -c $((16 * $KB)) -b $MB

    unify_run_test $app_name "$app_args" app_output
    rc=$?
    line_count=$(echo "$app_output" | wc -l)
    filename=$(get_filename $app_name "$app_args" ".app")

    test_expect_success POSIX "$app_name $app_args: (line_count=$line_count, rc=$rc)" '
        test $rc = 0 &&
        test $line_count = 8 &&
        test_path_has_file_per_process $CI_POSIX_MP $filename
    '

Sharness Helper Functions
^^^^^^^^^^^^^^^^^^^^^^^^^

There are also additional sharness functions for testing the examples available
when `t/ci/ci-functions.sh`_ is sourced. These are to be used with sharness_ for
testing the results of running the examples with or without using the
:ref:`Example Helper Functions <helper-label>`.

``process_is_running()``
""""""""""""""""""""""""

USAGE: ``process_is_running process_name seconds_before_giving_up``

Checks if a process with the given name is running on every host, retrying up to
a given number of seconds before giving up. This function overrides the
``process_is_running()`` function used by the UnifyFS unit tests. The primary
difference being that this function checks for the process on every host.

Expects two arguments:

- $1 - Name of a process to check for
- $2 - Number of seconds to wait before giving up

.. code-block:: BASH
    :emphasize-lines:

    test_expect_success "unifyfsd is running" '
        process_is_running unifyfsd 5
    '

``process_is_not_running()``
""""""""""""""""""""""""""""

USAGE: ``process_is_not_running process_name seconds_before_giving_up``

Checks if a process with the given name is not running on every host, retrying
up to a given number of seconds before giving up. This function overrides the
``process_is_not_running()`` function used by the UnifyFS unit tests. The primary
difference being that this function checks that the process is not running on
every host.

Expects two arguments:

- $1 - Name of a process to check for
- $2 - Number of seconds to wait before giving up

.. code-block:: BASH

    test_expect_success "unifyfsd is not running" '
        process_is_not_running unifyfsd 5
    '

``test_path_is_dir()``
""""""""""""""""""""""

USAGE: ``test_path_is_dir dir_name [optional]``

Checks that a directory with the given name exists and is accessible from each
host. Does NOT need to be a shared directory. This function overrides the
``test_path_is_dir()`` function in sharness.sh_, the primary difference being
that this function checks for the dir on every host in the allocation.

Takes once argument with an optional second:

- $1 - Path of the directory to check for
- $2 - Can be given to provide a more precise diagnosis

.. code-block:: BASH

    test_expect_success "$dir_name is an existing directory" '
        test_path_is_dir $dir_name
    '

``test_path_is_shared_dir()``
"""""""""""""""""""""""""""""

USAGE: ``test_path_is_shared_dir dir_name [optional]``

Check if same directory (actual directory, not just name) exists and is
accessible from each host.

Takes once argument with an optional second:

- $1 - Path of the directory to check for
- $2 - Can be given to provide a more precise diagnosis

.. code-block:: BASH

    test_expect_success "$dir_name is a shared directory" '
        test_path_is_shared_dir $dir_name
    '

``test_path_has_file_per_process()``
""""""""""""""""""""""""""""""""""""

USAGE: ``test_path_has_file_per_process dir_path file_name [optional]``

Check if the provided directory path contains a file-per-process of the provided
file name. Assumes the directory is a shared directory.

Takes two arguments with an optional third:

- $1 - Path of the shared directory to check for the files
- $2 - File name without the appended process number
- $3 - Can be given to provided a more precise diagnosis

.. code-block:: BASH

    test_expect_success "$dir_name has file-per-process of $file_name" '
        test_path_has_file_per_process $dir_name $file_name
    '

There are other helper functions available as well, most of which are being used
by the test suite itself. Details on these functions can be found in their
comments in `t/ci/ci-functions.sh`_.

.. explicit external hyperlink targets

.. _Bamboo: https://www.atlassian.com/software/bamboo
.. _GitLab: https://about.gitlab.com
.. _examples: https://github.com/LLNL/UnifyFS/tree/dev/examples/src
.. _libtap library: https://github.com/zorgnax/libtap
.. _lib/testutil.c: https://github.com/LLNL/UnifyFS/blob/dev/t/lib/testutil.c
.. _PDSH: https://github.com/chaos/pdsh
.. _sharness: https://github.com/chriscool/sharness
.. _sharness.d: https://github.com/LLNL/UnifyFS/tree/dev/t/sharness.d
.. _sharness.d/00-test-env.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/sharness.d/00-test-env.sh
.. _sharness.d/01-unifyfs-settings.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/sharness.d/01-unifyfs-settings.sh
.. _sharness.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/sharness.sh
.. _Spack: https://github.com/spack/spack
.. _t/ci: https://github.com/LLNL/UnifyFS/blob/dev/t/ci
.. _t/Makefile.am: https://github.com/LLNL/UnifyFS/blob/dev/t/Makefile.am
.. _t/sys/sysio_suite.c: https://github.com/LLNL/UnifyFS/blob/dev/t/sys/sysio_suite.c
.. _t/ci/100-writeread-tests.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/ci/100-writeread-tests.sh
.. _t/ci/ci-functions.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/ci/ci-functions.sh
.. _t/ci/RUN_CI_TESTS.sh: https://github.com/LLNL/UnifyFS/blob/dev/t/ci/RUN_CI_TESTS.sh
.. _Test Anything Protocol: https://testanything.org
.. _Travis CI: https://docs.travis-ci.com
