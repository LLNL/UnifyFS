# Integration Testing

The UnifyFS [examples](https://github.com/LLNL/UnifyFS/tree/dev/examples) are
being used as integration tests with continuous integration tools. These scripts
depend on [sharness](https://github.com/chriscool/sharness) which is set up in
the containing directory (`UnifyFS/t/`) and may not function properly if moved.

The numbered scripts are the setup and tests being run by `RUN_CI_TESTS.sh`.

`001-setup.sh`
: Checks for an installation of UnifyFS and sets up variables needed for
testing.

`002-start-server.sh`
: Starts unifyfsd and tests to ensure it is running.

`100-900`
: Scripts for testing the examples.

`990-stop-server.sh`
: Stops unifyfsd and cleans up.

The other scripts (`ci-functions.sh`, `setup-lsf.sh`, and `setup-slurm.sh`)
contain helper functions and variables used by the tests. Full details can be
found in [UnifyFS testing documentation](https://unifyfs.readthedocs.io/en/dev/testing.html#integration-tests).

## Quickstart

In order to run these scripts, make sure you are either submitting a batch job
to run them, or are first in an interactive allocation.

> **Note:** `mpirun` is currently not supported but coming soon.

To run all of the tests, simply run `./RUN_CI_TESTS.sh`.

E.g.:

```shell
$ ./RUN_CI_TESTS.sh
```

or

```shell
$ prove -v RUN_CI_TESTS.sh
```

In order to run individual tests, source the `001-setup.sh` script first,
followed by `002-start-server.sh`. Then source each desired script after that
preceded by `$UNIFYFS_CI_DIR`. When finished, source the `990-stop-server.sh`
script last.

E.g.:

```shell
$ . ./001-setup.sh
$ . $UNIFYFS_CI_DIR/002-start-server.sh
$ . $UNIFYFS_CI_DIR/100-writeread-tests.sh
$ . $UNIFYFS_CI_DIR/990-stop-server.sh
```

If additional tests are desired, create a script after the fashion of
`100-writeread-tests.sh` where the prefixed number indicates the desired order
for running the tests. Then source that script in `RUN_CI_TESTS.sh` in the
desired order.

### Environment Variables

There are environment variables that can be set to change the default behavior
of this testing suite. See our [testing
guide](https://unifyfs.readthedocs.io/en/dev/testing.html#configuration-variables)
for a full list.

## Developers

See our [Testing Guide](https://unifyfs.readthedocs.io/en/dev/testing.html#integration-tests)
for full documentation on writing and adding additional tests.
