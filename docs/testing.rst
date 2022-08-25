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

    # Various tests available to use inside test_expect_success/failure
    test_expect_success "Show various available tests" '
        test_path_is_dir /somedir
        test_must_fail test_dir_is_empty /somedir
        test_path_is_file /somedir/somefile
    '

    # Use test_set_prereq/test_have_prereq to conditionally skip tests
    [[ -n $(which h5cc 2>/dev/null) ]] && test_set_prereq HAVE_HDF5
    if test_have_prereq HAVE_HDF5; then
        # run HDF5 tests
    fi

    # Can also check for prereq in individual test
    test_expect_success HAVE_HDF5 "Run HDF5 test" '
        # Run HDF5 test
    '

    test_done

.. _C-tests-label:

C Program Tests
^^^^^^^^^^^^^^^

C programs use the `libtap library`_ to implement test cases. All available
testing functions are viewable in the `libtap README`_. Convenience functions
common to test cases written in C are implemented in the library
`lib/testutil.c`_. If your C program needs to use environment variables set by
sharness, it can be wrapped in a shell script that first sources
`sharness.d/00-test-env.sh`_ and `sharness.d/01-unifyfs-settings.sh`_. Your
wrapper shouldn't normally source sharness.sh_ itself because the TAP output
from sharness might conflict with that from libtap.

The most common way to implement a test with libtap is to use the ``ok()``
function. TODO test cases that demonstrate known breakage are surrounded by the
libtap library calls ``todo()`` and ``end_todo()``.

Here are some examples of libtap tests:

.. code-block:: C
    :linenos:

    #include "t/lib/tap.h"
    #include "t/lib/testutil.h"
    #include <string.h>

    int main(int argc, char *argv[])
    {
        int result;

        result = (1 == 1);
        ok(result, "1 equals 1: %d", result);

        /* Or put a function call directly in test */
        ok(somefunc() == 42, "somefunc() returns 42");
        ok(somefunc() == -1, "somefunc() should fail");

        /* Use pass/fail for more complex code paths */
        int x = somefunc();
        if (x > 0) {
            pass("somefunc() returned a valid value");
        } else {
            fail("somefunc() returned an invalid value");
        }

        /* Use is/isnt for string comparisions */
        char buf[64] = {0};
        ok(fread(buf, 12, 1, fd) == 1, "read 12 bytes into buf);
        is(buf, "hello world", "buf is \"hello world\"");

        /* Use cmp_mem to test first n bytes of memory */
        char* a = "foo";
        char* b = "bar";
        cmp_mem(a, b, 3);

        /* Use like/unlike to string match to a POSIX regex */
        like("stranger", "^s.(r).*\\1$", "matches the regex");

        /* Use dies_ok/lives_ok to test whether code causes an exit */
        dies_ok({int x = 0/0;}, "divide by zero crashes");

        /* Use todo for failing tests to be notified when they start passing */
        todo("Prove this someday");
        result = strcmp("P", "NP");
        ok(result == 0, "P equals NP: %d", result);
        end_todo;

        /* Use skip/end_skip when a feature isn't implemented yet, or to
        conditionally skip when a resource isn't available */
        skip(TRUE, 2, "Reason for skipping tests");
        ok(1);
        ok(2);
        end_skip;

        #ifdef HAVE_SOME_FEATURE
            ok(somefunc());
            ok(someotherfunc());
        #else
            skip(TRUE, 2, "Don't have SOME_FEATURE");
            end_skip;
        #endif

        done_testing();
    }

.. tip::

    Including the file and line number, as well as any useful variable values,
    in each test output can be very helpful when a test fails or needs to be
    debugged.

        .. code-block:: C

            ok(somefunc() == 42, "%s:%d somefunc() returns 42", __FILE__,
            __LINE__);

    Also note that ``errno`` is only set when an error occurs and is never set
    back to ``0`` implicitly by the system.
    When testing for a failure and using ``errno`` as part of the test,
    setting ``errno = 0`` before the test will ensure a previous test error
    will not affect the current test. In the following example, we also
    assign ``errno`` to another variable ``err`` for use in constructing the
    test message. This is needed because the ``ok()`` macro may use system
    calls that set ``errno``.

        .. code-block:: C

            int err, rc;
            errno = 0;
            rc = systemcall();
            err = errno;
            ok(rc == -1 && err == ENOTTY,
               "%s:%d systemcall() should fail (errno=%d): %s",
               __FILE__, __LINE__, err, strerror(err));

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

To manually run the UnifyFS unit test suite, simply run ``make check`` in a
single-node allocation from inside the t/ directory of wherever you built
UnifyFS. E.g., if you built in a separate build/ directory, then do:

.. code-block:: BASH

    $ cd build/t
    $ make check

If on a system where jobs are launched on a separate compute node, then use your
systems local MPI job launch command to run the unit tests:

.. code-block:: BASH

    $ cd build/t
    $ srun -N1 -n1 make check

If changes are made to existing files in the test suite, the tests can be run
again by simply doing ``make clean`` followed by another ``make check``.

Individual tests may be run by hand. The test *0001-setup.t* should
normally be run first to start the UnifyFS daemon. E.g., to run just the
*0100-sysio-gotcha.t* tests, do:

.. code-block:: BASH

    $ make check TESTS='0001-setup.t 0100-sysio-gotcha.t 9010-stop-unifyfsd.t 9999-cleanup.t'

.. note:: **Running Unit Tests from Spack Install**

    If using Spack to install UnifyFS there are two ways to manually run the
    units tests:

    1. Upon installation with Spack

        ``spack install -v --test=root unifyfs``

    2. Manually from Spack's build directory

        Open the Spack config file:

            ``spack config edit config``

        Provide Spack a staging path that is visible from a job allocation:

            .. code-block:: yaml

                config:
                  build_stage:
                  - /visible/path/allocated/node
                  # or build directly inside Spack's install directory
                  - $spack/var/spack/stage

        Include the ``--keep-stage`` option when installing:

            ``spack install --keep-stage unifyfs``

            ``spack cd unifyfs``

            ``cd spack-build/t``

        Run the tests from the package's build environment:

            ``spack build-env unifyfs make check``

The tests in https://github.com/LLNL/UnifyFS/tree/dev/t are run automatically
using `GitHub Actions`_ along with the :ref:`style checks <style-check-label>`
when a pull request is created or updated. All pull requests must pass these
tests before they will be accepted.

Interpreting the Results
^^^^^^^^^^^^^^^^^^^^^^^^

.. sidebar:: TAP Output

    .. image:: images/tap-output.png
        :align: center

After a test runs, its result is printed out consisting of its status followed
by its description and potentially a TODO/SKIP message. Once all the tests
have completed (either from being run manually or by `GitHub Actions`_), the overall
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
continuous integration tools such as Bamboo_ or `GitLab CI`_.

------------

-----------------
Integration Tests
-----------------

The UnifyFS examples_ are being used as integration tests with continuation
integration tools such as Bamboo_ or `GitLab CI`_.

To run any of these examples manually, refer to the :doc:`examples`
documentation.

------------

Configuration Variables
***********************

Along with the already provided :doc:`configuration` options/environment
variables, there are environment variables used by the integration testing
suite that can also be set in order to change the default behavior.

Key Variables
^^^^^^^^^^^^^

These environment variables can be set prior to sourcing the *t/ci/001-setup.sh*
script and will affect how the overall integration suite operates.

``UNIFYFS_INSTALL``
"""""""""""""""""""

USAGE: ``UNIFYFS_INSTALL=/path/to/dir/containing/UnifyFS/bin/directory``

The full path to the directory containing the *bin/* and *libexec/* directories
for your UnifyFS installation. Set this envar to prevent the integration tests
from searching for a UnifyFS installation automatically. Where the automatic
search starts can be altered by setting the ``$BASE_SEARCH_DIR`` variable.

``UNIFYFS_CI_NPROCS``
"""""""""""""""""""""

USAGE: ``UNIFYFS_CI_NPROCS=<number-of-process-per-node>``

The number of processes to use per node inside a job allocation. This defaults
to 1 process per node.  This can be adjusted if more processes are desired
on multiple nodes or multiple processes are desired on a single node.

``UNIFYFS_CI_TEMP_DIR``
"""""""""""""""""""""""

USAGE: ``UNIFYFS_CI_TEMP_DIR=/path/for/temporary/files/created/by/UnifyFS``

Can be used as a shortcut to set ``UNIFYFS_RUNSTATE_DIR`` and
``UNIFYFS_META_DB_PATH`` to the same path.  This envar defaults to
``UNIFYFS_CI_TEMP_DIR=${TMPDIR}/unifyfs.${USER}.${JOB_ID}``.

``UNIFYFS_CI_LOG_CLEANUP``
""""""""""""""""""""""""""

USAGE: ``UNIFYFS_CI_LOG_CLEANUP=yes|YES|no|NO``

In the event ``$UNIFYFS_LOG_DIR`` has **not** been set, the logs will be put in
``$SHARNESS_TRASH_DIRECTORY``, as set up by sharness.sh_, and cleaned up
automatically after the tests have run. The logs will be in a
*<system-name>_<jobid>/* subdirectory. Should any tests fail, sharness does not
clean up the trash directory for debugging purposes. Setting
``UNIFYFS_CI_LOG_CLEANUP=no|NO`` will move the *<system-name>_<jobid>/* logs
directory to ``$UNIFYFS_CI_DIR`` (the directory containing the integration
testing scripts) to allow them to persist even when all tests pass. This envar
defauls to ``yes``.

.. note::

    Setting ``$UNIFYFS_LOG_DIR`` will put all created logs in the designated path
    and will not clean them up.

``UNIFYFS_CI_HOST_CLEANUP``
"""""""""""""""""""""""""""

USAGE: ``UNIFYFS_CI_HOST_CLEANUP=yes|YES|no|NO``

After all tests have run, the nodes on which the tests were ran will
automatically be cleaned up. This cleanup includes ensuring ``unifyfsd`` has
stopped and deleting any files created by UnifyFS or its dependencies. Set
``UNIFYFS_CI_HOST_CLEANUP=no|NO`` to skip cleaning up. This envar defaults to
``yes``.

.. note::

    PDSH_ is required for cleanup and cleaning up is simply skipped if not
    found.

``UNIFYFS_CI_CLEANUP``
""""""""""""""""""""""

USAGE: ``UNIFYFS_CI_CLEANUP=yes|YES|no|NO``

Setting this to ``no|NO`` sets both ``$CI_LOG_CLEANUP`` and
``$UNIFYFS_CI_HOST_CLEANUP`` to ``no|NO``.

``UNIFYFS_CI_TEST_POSIX``
"""""""""""""""""""""""""

USAGE: ``UNIFYFS_CI_TEST_POSIX=yes|YES|no|NO``

Determines whether any ``<example-name>-posix`` tests should be run since they
require a real mountpoint to exist.

This envar defaults to ``no``. Setting this to ``yes`` will run the posix
version of tests along with the regular tests. When ``$UNIFYFS_MOUNTPOINT`` is
set to a existing directory, this option is set to ``no``. This is to allow
running the tests a first time with a fake mountpoint while the posix tests use
an existing mountpoint. Then the regular tests can be run again using an
existing mountpoint and the posix tests won't be run twice.

An example of testing a posix example can be see :ref:`below <posix-ex-label>`.

.. note::

    The posix mountpoint envar, ``UNIFYFS_CI_POSIX_MP``, is set to be located
    inside ``$SHARNESS_TRASH_DIRECTORY`` automatically and cleaned up
    afterwards. However, this envar can be set before running the integration
    tests as well. If setting this, ensure that it is a shared file system that
    all allocated nodes can see.

Additional Variables
^^^^^^^^^^^^^^^^^^^^

After sourcing the *t/ci/001-setup.sh* script there will be additional variables
available that may be useful when writing/adding additional tests.

Directory Structure
"""""""""""""""""""

File structure here is assuming UnifyFS was cloned to ``$HOME``.

``UNIFYFS_CI_DIR``
    Directory containing the CI testing scripts. *$HOME/UnifyFS/t/ci/*
``SHARNESS_DIR``
    Directory containing the base sharness scripts. *$HOME/UnifyFS/t/*
``UNIFYFS_SOURCE_DIR``
    Directory containing the UnifyFS source code. *$HOME/UnifyFS/*
``BASE_SEARCH_DIR``
    Parent directory containing the UnifyFS source code. Starting place to auto
    search for UnifyFS install when ``$UNIFYFS_INSTALL`` isn't provided. *$HOME/*

Executable Locations
""""""""""""""""""""

``UNIFYFS_BIN``
    Directory containing ``unifyfs`` and ``unifyfsd``. *$UNIFYFS_INSTALL/bin*
``UNIFYFS_EXAMPLES``
    Directory containing the compiled examples_. *$UNIFYFS_INSTALL/libexec*

Resource Managers
"""""""""""""""""

``JOB_RUN_COMMAND``
    The base MPI job launch command established according to the detected
    resource manager, number of allocated nodes, and ``$UNIFYFS_CI_NPROCS``.

    The LSF variables below will also affect the default version of this command
    when using that resource manager.
``JOB_RUN_ONCE_PER_NODE``
    MPI job launch command to only run a single process on each allocated node
    established according to the detected resource manager.
``JOB_ID``
    The ID assigned to the current CI job as established by the detected
    resource manager.

LSF
"""

Additional variables used by the LSF resource manager to determine how jobs are
launched with ``$JOB_RUN_COMMAND``. These can also be set prior to sourcing the
*t/ci/001-setup.sh* script and will affect how the integration tests run.

``UNIFYFS_CI_NCORES``
    Number of cores-per-resource-set to use. Defaults to 20.
``UNIFYFS_CI_NRS_PER_NODE``
    Number of resource-sets-per-node to use. Defaults to 1.
``UNIFYFS_CI_NRES_SETS``
    Total number of resource sets to use. Defaults to (number_of_nodes) *
    (``$UNIFYFS_CI_NRS_PER_NODE``).

Misc
""""

``KB``
    :math:`2^{10}`
``MB``
    :math:`2^{20}`
``GB``
    :math:`2^{30}`

------------

Running the Tests
*****************

.. attention::

    UnifyFS's integration test suite requires MPI and currently only supports
    ``srun`` and ``jsrun`` MPI launch commands.

UnifyFS's integration tests are primarly set up to run distinct suites of tests,
however they can also all be run at once or manually for more fine-grained
control.

The testing scripts in `t/ci`_ depend on sharness_, which is set up in the
containing *t/* directory. These tests will not function properly if moved or if
the sharness files cannot be found.

Before running any tests, ensure either compute nodes have been interactively
allocated or run via a batch job submission.

Make sure all :ref:`dependencies <spack-build-label>` are installed and loaded.

The *t/ci/RUN_CI_TESTS.sh* script is designed to simplify running various suites
of tests.

.. rubric:: ``RUN_CI_TESTS.sh`` Script

.. code-block:: Bash

    Usage: ./RUN_CI_TESTS.sh [-h] -s {all|[writeread,[write|read],pc,stage]} -t {all|[posix,mpiio]}

    Any previously set UnifyFS environment variables will take precedence.

    Options:
      -h, --help
             Print this help message

      -s, --suite {all|[writeread,[write|read],pc,stage]}
             Select the test suite(s) to be run
             Takes a comma-separated list of available suites

      -t, --type {all|[posix,mpiio]}
             Select the type(s) of each suite to be run
             Takes a comma-separated list of available types
             Required with --suite unless stage is the only suite selected

.. note:: **Running Integration Tests from Spack Build**

    Running the integration tests from a Spack_ installation of UnifyFS requires
    telling Spack to use a different location for staging the build in order to
    have the source files available from inside a job allocation.

    Open the Spack config file:

        ``spack config edit config``

    Provide a staging path that is visible to all nodes from a job allocations:

        .. code-block:: yaml

            config:
              build_stage:
              - /visible/path/from/all/allocated/nodes
              # or build directly inside Spack's install directory
              - $spack/var/spack/stage

    Include the ``--keep-stage`` option when installing:

        ``spack install --keep-stage unifyfs``

    Allocate compute nodes and spawn a new shell containing the package's build
    environment:

        ``spack build-env unifyfs bash``

    Run the integration tests:

        ``spack load unifyfs``

        ``spack cd unifyfs``

        ``cd t/ci``

        # Run tests using any of the following formats

Individual Suites
^^^^^^^^^^^^^^^^^

To run individual test suites, indicate the desired suite(s) and type(s) when
running *RUN_CI_TESTS.sh*. E.g.:

.. code-block:: BASH

    $ ./RUN_CI_TESTS.sh -s writeread -t mpiio

or

.. code-block:: BASH

    $ prove -v RUN_CI_TESTS.sh :: -s writeread -t mpiio

The ``-s|--suite`` and ``-t|--type`` options flag which set(s) of tests to run.
Each suite (aside from ``stage``) requires a type to be selected as well. Note
that if ``all`` is selected, the other arguments are redundant. If the ``read``
suite is selected, then the ``write`` argument is redundant.

Available suites: all|[writeread,[write,read],pc,stage]
  all:       run all suites
  writeread: run writeread tests
  write:     run write tests only (redundant if read also set)
  read:      run write then read tests (all-hosts producer-consumer tests)
  pc:        run producer-consumer tests (disjoint sets of hosts)
  stage:     run stage tests (type not required)

Available types: all|[posix,mpiio]
  all:   run all types
  posix: run posix versions of above suites
  mpiio: run mpiio versions of above suites

All Tests
^^^^^^^^^

.. warning::

  If running all or most tests within a single allocation, a large amount of
  time and storage space will be required. Even if enough of both are available,
  it is still possible the run may hit other limitations (e.g.,
  ``client_max_files``, ``client_max_active_requests``,
  ``server_max_app_clients``). To avoid this, run individual suites from
  separate job allocations.

To run all of the tests, run *RUN_CI_TESTS.sh* with the all suites and types
options.

.. code-block:: BASH

    $ ./RUN_CI_TESTS.sh -s all -t all

or

.. code-block:: BASH

    $ prove -v RUN_CI_TESTS.sh :: -s all -t all

Subsets of Individual Suites
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Subsets of individual test suites can be run manually. This can be useful when
wanting more fine-grained control or for testing a specific configuration. To
run manually, the testing functions and variables need to be set up first and
then the UnifyFS servers need to be started.

First source the *t/ci/001-setup.sh* script whereafter sharness will change
directories to the ``$SHARNESS_TRASH_DIRECTORY``. To account for this, prefix
each subsequent script with ``$UNIFYFS_CI_DIR/`` when sourcing. Start the
servers next by sourcing *002-start-server.sh* followed by each desired test
script. When finished, source *990-stop-server.sh* last to stop the servers,
report the results, and clean up.

.. code-block:: BASH

    $ . ./001-setup.sh
    $ . $UNIFYFS_CI_DIR/002-start-server.sh
    $ . $UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --shuffle --mpiio
    $ . $UNIFYFS_CI_DIR/990-stop-server.sh

The various CI test suites can be run multiple times with different behaviors.
These behaviors are continually being extended. The `-h|--help` option for each
script can show what alternate behaviors are currently implemented along with
additional information for that particular suite.

.. code-block:: BASH

    [prompt]$ ./100-writeread-tests.sh --help
    Usage: 100-writeread-tests.sh [options...]

      options:
        -h, --help        print help message
        -l, --laminate    laminate between writing and reading
        -M, --mpiio       use MPI-IO instead of POSIX I/O
        -x, --shuffle     read different data than written

------------

Adding New Tests
****************

In order to add additional tests for different workflows, create a script after
the fashion of `t/ci/100-writeread-tests.sh`_ where the prefixed number
indicates the desired order for running the tests. Then source that script in
`t/ci/RUN_CI_TESTS.sh`_ in the desired order. The different test suite scripts
themselves can also be edited to add/change the number, types, and various
behaviors each suite will execute.

Just like the helpers functions found in sharness.d_, there are continuous
integration helper functions (see :ref:`below <helper-label>` for more details)
available in `t/ci/ci-functions.sh`_. These exist to help make adding new tests
as simple as possible.

One particularly useful function is ``unify_run_test()``. Currently, this
function is set up to work for the *write*, *read*, *writeread*, and
*checkpoint-restart* examples. This function sets up the MPI job run command and
default options as well as any default arguments wanted by all examples. See
:ref:`below <unify-run-test-label>` for details.

.. _helper-label:

Testing Helper Functions
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
automatically sets any options that are always wanted (-vkfo as well as -U and
the appropriate -m if posix test or not). The stderr output file is also created
(based on the filename that is autogenerated) and the appropriate option is set
for the MPI job run command.

Args that can be passed in are ([-cblnpx][-A|-L|-M|-N|-P|-S|-V]). All other args
(see :ref:`Running the Examples <run-ex-label>`) are set automatically,
including the outfile and filename (which are generated based on the input
``$app_name`` and ``$app_args``).

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

Builds and returns the filename with the provided suffix based on the input
app_name and app_args.

The filename in ``$UNIFYFS_MOUNTPOINT`` will be given a ``.app`` suffix.

This allows tests to get what the filename will be in advance if called
from a test suite. This can be used for posix tests to ensure the file showed
up in the mount point, as well as for read, cp, stat tests that potentially need
the filename from a previous test prior to running.

Error logs and outfiles are also created with this filename, with a ``.err`` or
``.out`` suffix respectively, and placed in the logs directory.

Returns a string with the spaces removed and hyphens replaced by underscores.

.. code-block:: BASH

    get_filename write-static "-p n1 -n 32 -c 1024 -b 1048576" ".app"
    write-static_pn1_n32_c1KB_b1MB.app

Some uses cases may be:

- posix tests where the file existence is checked for after a test was run
- read, cp, or stat tests where an already existing filename from a prior test
  might be needed

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
        test_path_has_file_per_process $UNIFYFS_CI_POSIX_MP $filename
    '

Additional Functions
""""""""""""""""""""

There are other convenience functions used bythat my be helpful in writing/adding tests are also
found in `t/ci/ci-functions.sh`_:

``find_executable()``
    USAGE: ``find_executable abs_path *file_name|*path/file_name [prune_path]``

    Locate the desired executable file when provided an absolute path of where
    to start searching, the name of the file with an optional preceding path,
    and an optional prune_path, or path to omit from the search.

    Returns the path of the first executable found with the given name and
    optional prefix.
``elapsed_time()``
    USAGE: ``elapsed_time start_time_in_seconds end_time_in_seconds``

    Calculates the elapsed time between two given times.

    Returns the elapsed time formatted as HH:MM:SS.
``format_bytes()``
    USAGE: ``format_bytes int``

    Returns the input bytes formatted as KB, MB, or GB (1024 becomes 1KB).

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
.. _GitHub Actions: https://docs.github.com/en/actions
.. _GitLab CI: https://about.gitlab.com
.. _examples: https://github.com/LLNL/UnifyFS/tree/dev/examples/src
.. _libtap library: https://github.com/zorgnax/libtap
.. _libtap README: https://github.com/zorgnax/libtap/blob/master/README.md
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
