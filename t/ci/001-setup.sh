#!/bin/sh

# This script checks for an installation of UnifyFS (module/spack loaded, in the
# UNIFYFS_INSTALL envar, or in the parent directory of this source code) and
# then sets up variables needed for testing.

test_description="Set up UnifyFS testing environment"

SETUP_USAGE="$(cat <<EOF
usage: ./001-setup.sh [-h|--help]

Run the setup for running smaller subsets of tests manually.
Before doing so, ensure compute nodes have been interactively allocated or run
via a batch job submission.

To run, first source 001-setup.sh followed by 002-start-server.sh.
Then source any desired test files. Lastly, source 990-stop-server.sh.

Any previously set UnifyFS environment variables will take precedence.

E.g.:
    $ . ./001-setup.sh
    $ . \$UNIFYFS_CI_DIR/002-start-server.sh
    $ . \$UNIFYFS_CI_DIR/100-writeread-tests.sh --laminate --mpiio
    $ . \$UNIFYFS_CI_DIR/990-stop-server.sh

For information on how to run all or full suites of tests, run
'./RUN_CI_TESTS.sh -h'.

Refer to the UnifyFS Testing Guide for more information:
https://unifyfs.readthedocs.io/en/dev/testing.html#subsets-of-individual-suites
EOF
)"

while [[ $# -gt 0 ]]
do
    case $1 in
        -h|--help)
            echo "$SETUP_USAGE"
            exit
            ;;
        *)
            echo "usage: ./001-setup.sh -h|--help"
            exit 1
            ;;
    esac
done

########## Set up messages and vars needed before sourcing sharness  ##########

[[ -z $infomsg ]] && infomsg="-- UNIFYFS JOB INFO:"
[[ -z $errmsg ]] && errmsg="!!!! UNIFYFS JOB ERROR:"

export TMPDIR=${TMPDIR:-/tmp}
export SYSTEM_NAME=$(echo $(hostname) | sed -r 's/(^[[:alpha:]]*)(.*)/\1/')


########## Set up sharness, variables, and functions for TAP testing  ##########

# Set up sharness variables and functions for TAP testing.
echo "$infomsg Setting up sharness"
UNIFYFS_CI_DIR=${UNIFYFS_CI_DIR:-$(dirname "$(readlink -fm $BASH_SOURCE)")}
SHARNESS_DIR="$(dirname "$UNIFYFS_CI_DIR")"
UNIFYFS_SOURCE_DIR="$(dirname "$SHARNESS_DIR")"
BASE_SEARCH_DIR=${BASE_SEARCH_DIR:-"$(dirname "$UNIFYFS_SOURCE_DIR")"}
echo "$infomsg UNIFYFS_CI_DIR: $UNIFYFS_CI_DIR"
echo "$infomsg SHARNESS_DIR: $SHARNESS_DIR"
echo "$infomsg UNIFYFS_SOURCE_DIR: $UNIFYFS_SOURCE_DIR"
echo "$infomsg BASE_SEARCH_DIR: $BASE_SEARCH_DIR"

SHARNESS_TEST_DIRECTORY=${SHARNESS_TEST_DIRECTORY:-$UNIFYFS_CI_DIR}
source ${SHARNESS_DIR}/sharness.sh
source $SHARNESS_DIR/sharness.d/02-functions.sh
source $UNIFYFS_CI_DIR/ci-functions.sh


########## Locate UnifyFS install and examples ##########

# Check if unifyfs is loaded and recognized command-line utility; if so, use it
# If not, check if UNIFYFS_INSTALL was set to the install directory and use it
# If neither, do simple auto-search in $BASE_SEARCH_DIR to check for unifyfsd
# If none of the above, fail out.
if [[ -n $(which unifyfs 2>/dev/null) ]]; then
    # If unifyfs is a loaded module, use it and set UNIFYFS_INSTALL
    echo "$infomsg Using unifyfs module"
    unifyfs_bin_dir=$(dirname "$(readlink -fm "$(which unifyfs)")")
    UNIFYFS_INSTALL=$(dirname "$(readlink -fm $unifyfs_bin_dir)")
    UNIFYFS_CLU="unifyfs"
elif [[ -n $UNIFYFS_INSTALL ]]; then
    # $UNIFYFS_INSTALL directory was provided
    UNIFYFS_BIN="$UNIFYFS_INSTALL/bin"
    UNIFYFS_CLU="${UNIFYFS_BIN}/unifyfs"
elif [[ -z $UNIFYFS_INSTALL ]]; then
    # Search $BASE_SEARCH_DIR for UnifyFS install directory if envar wasn't set
    echo "$infomsg Searching for UnifyFS install directory..."
    # Search for unifyfsd starting in $BASE_SEARCH_DIR and omitting SPACK_ROOT
    unifyfsd_exe="$(find_executable $BASE_SEARCH_DIR "*/bin/unifyfsd"\
                    $SPACK_ROOT)"
    if [[ -x $unifyfsd_exe ]]; then
        # Set UNIFYFS_INSTALL to the dir containing bin/ and libexec/
        UNIFYFS_INSTALL="$(dirname "$(dirname "$unifyfsd_exe")")"
        UNIFYFS_BIN="$UNIFYFS_INSTALL/bin"
        UNIFYFS_CLU="${UNIFYFS_BIN}/unifyfs"
    else # unifyfsd executable not found
        echo >&2 "$errmsg Unable to find UnifyFS install directory"
        echo >&2 "$errmsg Set \$UNIFYFS_INSTALL to the directory containing" \
                 "bin/ and libexec/ or \`spack install unifyfs\`"
        exit 1
    fi
fi

# Make sure UNIFYFS_INSTALL and libexec/ exist
if [[ -d $UNIFYFS_INSTALL && -d ${UNIFYFS_INSTALL}/libexec ]]; then
    UNIFYFS_EXAMPLES="$UNIFYFS_INSTALL/libexec"
    echo "$infomsg Using UnifyFS install directory: $UNIFYFS_INSTALL"
    echo "$infomsg Using UnifyFS command-line utilty: $UNIFYFS_CLU"
    echo "$infomsg Using UnifyFS examples directory: $UNIFYFS_EXAMPLES"
else
    echo >&2 "$errmsg Load the unifyfs module or" \
    echo >&2 "$errmsg Ensure \$UNIFYFS_INSTALL is set and is the directory" \
             "containing bin/ and libexec/"
    exit 1
fi

########## Determine job launcher and source associated setup ##########

# Source envar, functions, and set up JOB_RUN_COMMAND if lsf, slurm, or mpirun
# TODO: mpirun compatibility
echo "$infomsg Finding job launcher"
if [[ -n $(which jsrun 2>/dev/null) ]]; then
    source $UNIFYFS_CI_DIR/setup-lsf.sh
elif [[ -n $(which srun 2>/dev/null) ]]; then
    source $UNIFYFS_CI_DIR/setup-slurm.sh
else
    echo >&2 "$errmsg Failed to find a suitable parallel job launcher"
    exit 1
fi
echo "$infomsg JOB_RUN_COMMAND established: $JOB_RUN_COMMAND"


########## Set up CI and UNIFYFS configuration variables ##########

# Turn up log verbosity
export UNIFYFS_LOG_VERBOSITY=${UNIFYFS_LOG_VERBOSITY:-5}

# Set up location for logs and potentially auto cleanup if user didn't provide
# an alternate location for the logs
if [[ -z $UNIFYFS_LOG_DIR ]]; then
    # User can choose to not cleanup logs on success
    export UNIFYFS_CI_LOG_CLEANUP=${UNIFYFS_CI_LOG_CLEANUP:-yes}
    # If no log cleanup, move logs to $UNIFYFS_CI_DIR
    if [[ $UNIFYFS_CI_LOG_CLEANUP =~ ^(no|NO)$ ]] || \
       [[ $UNIFYFS_CI_CLEANUP =~ ^(no|NO)$ ]]
    then
        logdir=$UNIFYFS_CI_DIR/${SYSTEM_NAME}_${JOB_ID}_logs
    else # else put logs in sharness trash dir that sharness deletes
        logdir=$SHARNESS_TRASH_DIRECTORY/${SYSTEM_NAME}_${JOB_ID}_logs
        echo "$infomsg Set UNIFYFS_CI_LOG_CLEANUP=no to keep logs when all" \
             "tests pass"
    fi
fi
export UNIFYFS_LOG_DIR=${UNIFYFS_LOG_DIR:-$logdir}
mkdir -p $UNIFYFS_LOG_DIR
echo "$infomsg Logs are in UNIFYFS_LOG_DIR: $UNIFYFS_LOG_DIR"

# sharedfs
export UNIFYFS_SHAREDFS_DIR=${UNIFYFS_SHAREDFS_DIR:-$UNIFYFS_LOG_DIR}
echo "$infomsg UNIFYFS_SHAREDFS_DIR set as $UNIFYFS_SHAREDFS_DIR"

# daemonize
export UNIFYFS_DAEMONIZE=${UNIFYFS_DAEMONIZE:-off}

# temp
nlt=${TMPDIR}/unifyfs.${USER}.${SYSTEM_NAME}.${JOB_ID}
export UNIFYFS_CI_TEMP_DIR=${UNIFYFS_CI_TEMP_DIR:-$nlt}
$JOB_RUN_ONCE_PER_NODE mkdir -p $UNIFYFS_CI_TEMP_DIR
export UNIFYFS_RUNSTATE_DIR=${UNIFYFS_RUNSTATE_DIR:-$UNIFYFS_CI_TEMP_DIR}
echo "$infomsg UNIFYFS_RUNSTATE_DIR set as $UNIFYFS_RUNSTATE_DIR"

# storage
nls=$nlt
export UNIFYFS_LOGIO_SPILL_SIZE=${UNIFYFS_LOGIO_SPILL_SIZE:-$((16 * GB))}
export UNIFYFS_LOGIO_SPILL_DIR=${UNIFYFS_LOGIO_SPILL_DIR:-$nls}
echo "$infomsg UNIFYFS_LOGIO_SPILL_SIZE set as $UNIFYFS_LOGIO_SPILL_SIZE"
echo "$infomsg UNIFYFS_LOGIO_SPILL_DIR set as $UNIFYFS_LOGIO_SPILL_DIR"

export UNIFYFS_SERVER_MAX_APP_CLIENTS=${UNIFYFS_SERVER_MAX_APP_CLIENTS:-512}

########## Set up mountpoints and sharness testing prereqs ##########

# Running tests with UNIFYFS_MOUNTPOINT set to a real dir will disable posix
# tests unless user sets UNIFYFS_CI_TEST_POSIX=yes
export UNIFYFS_MP=${UNIFYFS_MOUNTPOINT:-/unifyfs}
# If UNIFYFS_MOUNTPOINT is real dir, disable posix tests (unless user wants it)
# and set REAL_MP prereq to enable test that checks if UNIFYFS_MOUNTPOINT is
# empty
if [[ -d $UNIFYFS_MP ]]; then
    export UNIFYFS_CI_TEST_POSIX=no
    test_set_prereq REAL_MP
fi
echo "$infomsg UNIFYFS_MOUNTPOINT established: $UNIFYFS_MP"

export UNIFYFS_CI_TEST_POSIX=${UNIFYFS_CI_TEST_POSIX:-no}
# Set up a real mountpoint for posix tests to write files to and allow tests to
# check that those files exist
if [[ ! $UNIFYFS_CI_TEST_POSIX =~ ^(no|NO)$ ]]; then
    if [[ -z $UNIFYFS_CI_POSIX_MP ]]; then
        # needs to be a shared file system
        pmp=${SHARNESS_TRASH_DIRECTORY}/unify_posix_mp.${SYSTEM_NAME}.${JOB_ID}
    fi
    export UNIFYFS_CI_POSIX_MP=${UNIFYFS_CI_POSIX_MP:-$pmp}
    mkdir -p $UNIFYFS_CI_POSIX_MP
    echo "$infomsg UNIFYFS_CI_POSIX_MP established: $UNIFYFS_CI_POSIX_MP"

    # Set test_posix prereq
    test_set_prereq TEST_POSIX
fi

# prereq for pdsh for cleaning hosts
[[ -n $(which pdsh 2>/dev/null) ]] && test_set_prereq PDSH

# skip cleanup_hosts test in 990-stop_server.sh if cleanup is not desired
export UNIFYFS_CI_HOST_CLEANUP=${UNIFYFS_CI_HOST_CLEANUP:-yes}
if ! [[ $UNIFYFS_CI_HOST_CLEANUP =~ ^(no|NO)$ ]] || \
     [[ $UNIFYFS_CI_CLEANUP =~ ^(no|NO)$ ]]; then
    test_set_prereq CLEAN
fi

# capture environment after all job setup completed
env &> ${UNIFYFS_LOG_DIR}/job.environ
