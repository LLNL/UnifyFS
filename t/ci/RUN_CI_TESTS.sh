#!/bin/sh

test_description="Unify Integration Testing Suite"

RUN_CI_TESTS_USAGE="$(cat <<EOF
Usage: ./RUN_CI_TESTS.sh [-h] -s {all|[writeread,[write|read],pc,stage]} -t {all|[posix,mpiio]}

Run the entire integration test suite or run select suites of UnifyFS TAP tests.
To manually run individual tests, run './001-setup.sh -h' first.

Before running any tests, ensure compute nodes have been interactively allocated
or run via a batch job submission.

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

NOTES

  INDIVIDUAL SUITES

  To run individual test suites, indicate the desired suite(s) and type(s)

  E.g.:
      $ ./RUN_CI_TESTS.sh -s writeread,read,pc -t mpiio
    or
      $ prove -v RUN_CI_TESTS.sh :: -s writeread,read,pc -t mpiio

  The suite and type options flag which set(s) of tests to run. Each suite
  (aside from "stage") requires a type to be selected as well. Note that if
  "all" is selected, the other arguments are redundant. If the "read" suite is
  selected, the "write" argument is redundant.

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

  ALL TESTS

  To run all of the tests, run RUN_CI_TESTS.sh with all suites and types

  E.g.:
      $ ./RUN_CI_TESTS.sh -s all -t all
    or
      $ prove -v RUN_CI_TESTS.sh :: -s all -t all

  Warning: If running all or most tests within a single allocation, a large
  amount of time and storage space is required. Even if enough of both are
  available, it is still possible the run may hit other limitations (e.g.,
  client_max_files, client_max_active_requests, server_max_app_clients). To
  avoid this, run individual suites from separate allocations.

  SUBSETS OF INDIVIDUAL SUITES

  For more fine-grained control, subsets of the above testing suites can be run
  manually by first sourcing 001-setup.sh followed by 002-start-server.sh.
  Then source any desired test files. Lastly, source 990-stop-server.sh.

  E.g.:
      $ . ./001-setup.sh
      $ . \$UNIFYFS_CI_DIR/002-start-server.sh
      $ . \$UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --mpiio
      $ . \$UNIFYFS_CI_DIR/990-stop-server.sh

Refer to the UnifyFS Testing Guide for more information:
https://unifyfs.readthedocs.io/en/dev/testing.html#integration-tests
EOF
)"

# Check if local environment has `getopt`
getopt --test > /dev/null
if [ $? != 4 ]; then
    echo "ERROR: `getopt --test` failed in this environment"
    exit 1
fi

# At least one parameter is required. Error out if none detected
if [ "$#" -lt "1" ]; then
    echo "ERROR: No input parameters detected. At least one parameter required"
    echo "Run './RUN_CI_TESTS.sh -h' for help"
    exit 1
fi

# Set up short and long options
SHORTOPTS=hs:t:
LONGOPTS=help,suite:,type:
TEMP=$(getopt -o $SHORTOPTS -l $LONGOPTS --name "$0" -- "$@")
if [ $? != 0 ]; then
    # getopt has complained about wrong arguments to stdout
    echo "Run './RUN_CI_TESTS.sh -h' for help"
    exit 1
fi

eval set -- "$TEMP"

# Suite and type envars for determining which tests to run, initialized to false
suite_envars=(WRITEREAD WRITE READ PC STAGE)
for i in "${suite_envars[@]}"; do export ${i}=false; done
type_envars=(POSIX MPIIO)
for j in "${type_envars[@]}"; do export ${j}=false; done

SUITE=
while true; do
  case "$1" in
    -h|--help ) echo "$RUN_CI_TESTS_USAGE"; exit ;;
    -s|--suite )
        SUITE="set"
        suites=(${2//,/ })
        shift 2
        ;;
    -t|--type )
        [ "$SUITE" != "set" ] &&
          { echo "USAGE ERROR: -t|--type requested but no -s|--suite selected"
            echo "Run './RUN_CI_TESTS.sh -h' for help"
            exit 1
          } || types=(${2//,/ })
        shift 2
        ;;
    -- ) shift; break ;;
    * ) echo "Unknown option error"; exit 2 ;;
  esac
done

# Verify desired suite(s)/type(s) are valid and set flags for which tests to run
if [[ ${#suites[@]} -eq 1 && "${suites[0]}" == "stage" ]]; then
    # Stage was only suite selected so type doesn't matter
    export STAGE=true
else
    # All suites (except stage) require a type to be specified
    if [[ ${#types[@]} -eq 0 ]]; then
        echo "ERROR: Requested suite(s) requires a -t|--type to be designated"
        echo "Run './RUN_CI_TESTS.sh -h' for help"
        exit 1
    fi

    # If "all" then other values are redundant. If read then write is redundant
    for s in "${suites[@]}"; do
        # Ensure suite is a valid value
        if [[ ! "$s" =~ ^(all|writeread|write|read|pc|stage)$ ]]; then
            echo "Error: suite arg ($s) not recognized"
            echo "Run './RUN_CI_TESTS.sh -h' for help"
            exit 1
        elif [ "$s" == "all" ]; then # Flag all suites to be run
            for i in "${suite_envars[@]}"; do export ${i^^}=true; done
        else # Flag this suite to be run
            export ${s^^}=true
        fi
    done

    # If "all" then other values are redundant
    for t in "${types[@]}"; do
        # Ensure type is a valid value
        if [[ ! "$t" =~ ^(all|posix|mpiio)$ ]]; then
            echo "Error: type arg ($t) not recognized"
            echo "Run './RUN_CI_TESTS.sh -h' for help"
            exit 1
        elif [ "$t" == "all" ]; then # Flag all types to be run
            for j in "${type_envars[@]}"; do export ${j^^}=true; done
        else # Flag this type to be run
            export ${t^^}=true
        fi
    done
fi


### Start running tests ###

SECONDS=0
start_time=$SECONDS
echo "Started RUN_CI_TESTS.sh @: $(date)"

# Set up UNIFYFS_CI_DIR if this script is called first
UNIFYFS_CI_DIR=${UNIFYFS_CI_DIR:-"$(dirname "$(readlink -fm $BASH_SOURCE)")"}

# If not set, tests can be run individually
# test_done gets called in 990-stop-server.sh if this is not set.
UNIFYFS_CI_TESTS_FULL_RUN=true

# setup testing
source $UNIFYFS_CI_DIR/001-setup.sh

# start unifyfsd
source $UNIFYFS_CI_DIR/002-start-server.sh

# determine time setup took
setup_time=$SECONDS
echo "Setup time -- $(elapsed_time start_time setup_time)"

##############################################################################
# If additional tests are desired, create a script after the fashion of
# 100-writeread-tests.sh where the prefixed number indicates the desired order
# for running the tests. Then source that script between here and the final
# testing time (before 990-stop-server.sh) in the desired order to run them.
##############################################################################

##### WRITEREAD test suite #####

if [ "$WRITEREAD" == "true" ]; then
  ### WRITEREAD-POSIX test suite ###
  if [ "$POSIX" == true ]; then
    echo "Running WRITEREAD-POSIX test suite"
    # POSIX-IO writeread example tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate

    # POSIX-IO writeread example with I/O shuffle tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --shuffle

    # POSIX-IO writeread example w/out laminate tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh

    # POSIX-IO writeread example w/out laminate tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --shuffle
    echo "Finished WRITEREAD-POSIX test suite"
  fi

  ### WRITEREAD-MPIIO test suite ###
  if [ "$MPIIO" == "true" ]; then
    echo "Running WRITEREAD-MPIIO test suite"
    # MPI-IO writeread example tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --mpiio

    # MPI-IO writeread example with I/O shuffle tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --shuffle --mpiio

    # MPI-IO writeread example w/out laminate tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --mpiio

    # MPI-IO writeread example w/out laminate tests
    source $UNIFYFS_CI_DIR/100-writeread-tests.sh --shuffle --mpiio
    echo "Finished WRITEREAD-MPIIO test suite"
  fi
fi

##### WRITE/READ test suite  #####
# Producer-consumer tests on all hosts

if [ "$WRITE" == "true" ] || [ "$READ" == "true" ]; then
  if [ "$POSIX" == true ]; then
    echo "Running WRITE/READ-POSIX test suite"
    # posix-io write example tests
    source $UNIFYFS_CI_DIR/110-write-tests.sh --laminate

    if [ "$READ" == "true" ]; then
        # posix-io read example tests
        source $UNIFYFS_CI_DIR/120-read-tests.sh --laminate
    fi

    # posix-io write example w/out laminate tests
    source $UNIFYFS_CI_DIR/110-write-tests.sh

    if [ "$READ" == "true" ]; then
        # posix-io read example w/out laminate tests
        source $UNIFYFS_CI_DIR/120-read-tests.sh
    fi
    echo "Finished WRITE/READ-POSIX test suite"
  fi

  if [ "$MPIIO" == "true" ]; then
    echo "Running WRITE/READ-MPIIO test suite"
    # MPI-IO write example tests
    source $UNIFYFS_CI_DIR/110-write-tests.sh --laminate --mpiio

    if [ "$READ" == "true" ]; then
        # MPI-IO read example tests
        source $UNIFYFS_CI_DIR/120-read-tests.sh --laminate --mpiio
    fi

    # MPI-IO write example w/out laminate tests
    source $UNIFYFS_CI_DIR/110-write-tests.sh --mpiio

    if [ "$READ" == "true" ]; then
        # MPI-IO read example w/out laminate tests
        source $UNIFYFS_CI_DIR/120-read-tests.sh --mpiio
    fi
    echo "Finished WRITE/READ-MPIIO test suite"
  fi
fi

##### Producer-Consumer workload test suite  #####
# Producer-consumer tests disjoint sets of hosts

if [ "$PC" == "true" ]; then
  ### PC-POSIX test suite ###
  if [ "$POSIX" == true ]; then
    echo "Running PC-POSIX test suite"
    # POSIX-IO producer-consumer tests
    source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh --laminate

    # POSIX-IO producer-consumer w/out laminate tests
    source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh
    echo "Finished PC-POSIX test suite"
  fi

  ### PC-MPIIO test suite ###
  if [ "$MPIIO" == "true" ]; then
    echo "Running PC-MPIIO test suite"
    # MPI-IO producer-consumer tests
    source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh --laminate --mpiio

    # MPI-IO producer-consumer w/out laminate tests
    source $UNIFYFS_CI_DIR/300-producer-consumer-tests.sh --mpiio
    echo "Finished PC-MPIIO test suite"
  fi
fi

##### unifyfs-stage test suite #####

if [ "$STAGE" == "true" ]; then
  echo "Running STAGE test suite"
  source $UNIFYFS_CI_DIR/800-stage-tests.sh
  echo "Finished STAGE test suite"
fi

##############################################################################
# DO NOT add additional tests after this point
##############################################################################
# determine time testing took
testing_time=$SECONDS
echo "Testing time -- $(elapsed_time setup_time testing_time)"

# stop unifyfsd and cleanup
source $UNIFYFS_CI_DIR/990-stop-server.sh

end_time=$SECONDS
echo "All done @ $(date)"
echo "Total run time -- $(elapsed_time start_time end_time)"

test_done
exit 0
