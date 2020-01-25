#!/bin/sh

# This script checks for an installation of UnifyFS (either with Spack or in
# $HOME/UnifyFS/install) and then sets up variables needed for testing.
#
# All of this is done in this script so that tests can be run individually if
# desired. To run all tests simply run the RUN_TESTS.sh script. If Individual
# tests are desired to be run, source the 001-setup.sh script first, followed by
# 002-start-server.sh. Then source each desired script after that preceded by
# `$CI_DIR`. When finished, source the 990-stop-server.sh script last.
#
# E.g.:
#      $ . full/path/to/001-setup.sh
#      $ . $CI_DIR/002-start-server.sh
#      $ . $CI_DIR/100-writeread-tests.sh
#      $ . $CI_DIR/990-stop-server.sh
#
# To run all of the tests, simply run RUN_CI_TESTS.sh
#
# E.g.:
#     $ ./RUN_CI_TESTS.sh
#   or
#     $ prove -v RUN_CI_TESTS.sh
#
# Before doing either of these, make sure you have interactively allocated nodes
# or are submitting a batch job.

test_description="Set up UnifyFS testing environment"

SETUP_USAGE="$(cat <<EOF
usage: ./001-setup.sh -h|--help

You can run individually desired test files (i.e., 100-writeread-tests.sh and no
other tests) by first sourcing 001-setup.sh followed by 002-start-server.sh.
Then source any desired test files. Lastly, source 990-stop-server.sh.

E.g.:
    $ . full/path/to/001-setup.sh
    $ . $CI_DIR/002-start-server.sh
    $ . $CI_DIR/100-writeread-tests.sh
    $ . $CI_DIR/990-stop-server.sh

To run all of the tests, simply run RUN_CI_TESTS.sh.

E.g.:
    $ ./RUN_CI_TESTS.sh
  or
    $ prove -v RUN_CI_TESTS.sh

Before doing either of these, make sure you have interactively allocated nodes
or are submitting a batch job.
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

export CI_PROJDIR=${CI_PROJDIR:-$HOME}
export TMPDIR=${TMPDIR:-/tmp}
export SYSTEM_NAME=$(echo $(hostname) | sed -r 's/(^[[:alpha:]]*)(.*)/\1/')


########## Set up sharness, variables, and functions for TAP testing  ##########

# Set up sharness variables and functions for TAP testing.
echo "$infomsg Setting up sharness"
CI_DIR=${CI_DIR:-$(dirname "$(readlink -fm $BASH_SOURCE)")}
SHARNESS_DIR="$(dirname "$CI_DIR")"
echo "$infomsg CI_DIR: $CI_DIR"
echo "$infomsg SHARNESS_DIR: $SHARNESS_DIR"
source ${SHARNESS_DIR}/sharness.sh
source $SHARNESS_DIR/sharness.d/02-functions.sh
source $CI_DIR/ci-functions.sh


########## Locate UnifyFS install and examples ##########

# Check if we have Spack and if UnifyFS is installed.
# If don't have both, fall back to checking for non-spack install.
# If neither, fail out.
# Set UNIFYFS_INSTALL to skip searching.
echo "$infomsg Looking for UnifyFS install directory..."

# Look for UnifyFS install directory if the user didn't already set
# $UNIFYFS_INSTALL to the directory containing bin/ and libexec/
if [[ -z $UNIFYFS_INSTALL ]]; then
    # Check for $SPACK_ROOT and if unifyfs is installed
    if [[ -n $SPACK_ROOT && -d $(spack location -i unifyfs 2>/dev/null) ]];
    then
        # Might have a problem with variants and arch
        UNIFYFS_INSTALL="$(spack location -i unifyfs)"
    # Else search for unifyfsd starting in $CI_PROJDIR and omitting spack_root
    elif [[ -x $(find_executable $CI_PROJDIR "*/bin/unifyfsd" $SPACK_ROOT) ]];
    then
        # Set UNIFYFS_INSTALL to the dir containing bin/ and libexec/
        UNIFYFS_INSTALL="$(dirname "$(dirname \
            "$(find_executable $CI_PROJDIR "*/bin/unifyfsd" $SPACK_ROOT)")")"
    else
        echo >&2 "$errmsg Unable to find UnifyFS install directory"
        echo >&2 "$errmsg \`spack install unifyfs\`, set the" \
                 "\$UNIFYFS_INSTALL envar to the directory containing bin/" \
                 "and libexec/, or manually install to \$CI_PROJDIR/*"
        exit 1
    fi
fi

# Make sure UNIFYFS_INSTALL, bin/, and libexec/ exist
if [[ -d $UNIFYFS_INSTALL && -d ${UNIFYFS_INSTALL}/bin &&
      -d ${UNIFYFS_INSTALL}/libexec ]]; then
    echo "$infomsg Found UnifyFS install directory: $UNIFYFS_INSTALL"

    UNIFYFS_BIN="$UNIFYFS_INSTALL/bin"
    UNIFYFS_EXAMPLES="$UNIFYFS_INSTALL/libexec"
    echo "$infomsg Found UnifyFS bin directory: $UNIFYFS_BIN"
    echo "$infomsg Found UnifyFS examples directory: $UNIFYFS_EXAMPLES"
else
    echo >&2 "$errmsg Ensure \$UNIFYFS_INSTALL exists and is the directory" \
             "containing bin/ and libexec/"
fi

# Check for necessary Spack modules if Spack is detected
# Since GitLab Runners don't like this, just warn users running this by hand but
# don't fail out
if [[ -n $(which spack 2>/dev/null) ]]; then
    loaded_modules=$(module list 2>&1)
    modules="gotcha leveldb flatcc argobots mercury margo"
    for mod in $modules; do
        if ! [[ $(echo "$loaded_modules" | fgrep "$mod") ]]; then
            echo "$errmsg $mod not detected. Please 'spack load $mod'"
        fi
    done
fi


########## Determine job launcher and source associated setup ##########

# Source envar, functions, and set up JOB_RUN_COMMAND if lsf, slurm, or mpirun
# TODO: mpirun compatibility
echo "$infomsg Finding job launcher"
if [[ -n $(which jsrun 2>/dev/null) ]]; then
    source $CI_DIR/setup-lsf.sh
elif [[ -n $(which srun 2>/dev/null) ]]; then
    source $CI_DIR/setup-slurm.sh
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
    export CI_LOG_CLEANUP=${CI_LOG_CLEANUP:-yes}
    # If no log cleanup, move logs to $CI_DIR
    if [[ $CI_LOG_CLEANUP =~ ^(no|NO)$ || $CI_CLEANUP =~ ^(no|NO)$ ]]; then
        logdir=$CI_DIR/${SYSTEM_NAME}_${JOB_ID}_logs
    else # else put logs in sharness trash dir that sharness deletes
        logdir=$SHARNESS_TRASH_DIRECTORY/${SYSTEM_NAME}_${JOB_ID}_logs
        echo "$infomsg Set CI_LOG_CLEANUP=no to keep logs when all tests pass"
    fi
    mkdir -p $logdir
fi
export UNIFYFS_LOG_DIR=${UNIFYFS_LOG_DIR:-$logdir}
echo "$infomsg Logs are in UNIFYFS_LOG_DIR: $UNIFYFS_LOG_DIR"

export UNIFYFS_SHAREDFS_DIR=${UNIFYFS_SHAREDFS_DIR:-$UNIFYFS_LOG_DIR}
echo "$infomsg UNIFYFS_SHAREDFS_DIR set as $UNIFYFS_SHAREDFS_DIR"

# daemonize
export UNIFYFS_DAEMONIZE=${UNIFYFS_DAEMONIZE:-off}

# temp
nlt=${TMPDIR}/unifyfs.${USER}.${SYSTEM_NAME}.${JOB_ID}
export CI_TEMP_DIR=${CI_TEMP_DIR:-$nlt}
export UNIFYFS_RUNSTATE_DIR=${UNIFYFS_RUNSTATE_DIR:-$CI_TEMP_DIR}
export UNIFYFS_META_DB_PATH=${UNIFYFS_META_DB_PATH:-$CI_TEMP_DIR}
echo "$infomsg UNIFYFS_RUNSTATE_DIR set as $UNIFYFS_RUNSTATE_DIR"
echo "$infomsg UNIFYFS_META_DB_PATH set as $UNIFYFS_META_DB_PATH"
echo "$infomsg Set CI_TEMP_DIR to change both of these to same path"

# storage
nls=$nlt
export CI_STORAGE_DIR=${CI_STORAGE_DIR:-$nls}
export UNIFYFS_SPILLOVER_SIZE=${UNIFYFS_SPILLOVER_SIZE:-$((5 * GB))}
export UNIFYFS_SPILLOVER_ENABLED=${UNIFYFS_SPILLOVER_ENABLED:-yes}
export UNIFYFS_SPILLOVER_DATA_DIR=${UNIFYFS_SPILLOVER_DATA_DIR:-$CI_STORAGE_DIR}
export UNIFYFS_SPILLOVER_META_DIR=${UNIFYFS_SPILLOVER_META_DIR:-$CI_STORAGE_DIR}
echo "$infomsg UNIFYFS_SPILLOVER_DATA_DIR set as $UNIFYFS_SPILLOVER_DATA_DIR"
echo "$infomsg UNIFYFS_SPILLOVER_META_DIR set as $UNIFYFS_SPILLOVER_META_DIR"
echo "$infomsg Set CI_STORAGE_DIR to change both of these to same path"


########## Set up mountpoints and sharness testing prereqs ##########

# Running tests with UNIFYFS_MOUNTPOINT set to a real dir will disable posix
# tests unless user sets CI_TEST_POSIX=yes
export UNIFYFS_MP=${UNIFYFS_MOUNTPOINT:-/unifyfs}
# If UNIFYFS_MOUNTPOINT is real dir, disable posix tests (unless user wants it)
# and set REAL_MP prereq to enable test that checks if UNIFYFS_MOUNTPOINT is
# empty
if [[ -d $UNIFYFS_MP ]]; then
    export CI_TEST_POSIX=no
    test_set_prereq REAL_MP
fi
echo "$infomsg UNIFYFS_MOUNTPOINT established: $UNIFYFS_MP"

export CI_TEST_POSIX=${CI_TEST_POSIX:-yes}
# Set up a real mountpoint for posix tests to write files to and allow tests to
# check that those files exist
if [[ ! $CI_TEST_POSIX =~ ^(no|NO)$ ]]; then
    if [[ -z $CI_POSIX_MP ]]; then
        # needs to be a shared file system
        pmp=${SHARNESS_TRASH_DIRECTORY}/unify_posix_mp.${SYSTEM_NAME}.${JOB_ID}
        mkdir $pmp
    fi
    export CI_POSIX_MP=${CI_POSIX_MP:-$pmp}
    echo "$infomsg CI_POSIX_MP established: $CI_POSIX_MP"

    # Set test_posix prereq
    test_set_prereq TEST_POSIX
fi

# prereq for pdsh for cleaning hosts
[[ -n $(which pdsh 2>/dev/null) ]] && test_set_prereq PDSH

# skip cleanup_hosts test in 990-stop_server.sh if cleanup is not desired
export CI_HOST_CLEANUP=${CI_HOST_CLEANUP:-yes}
if ! [[ $CI_HOST_CLEANUP =~ ^(no|NO)$ || $CI_CLEANUP =~ ^(no|NO)$ ]]; then
    test_set_prereq CLEAN
fi

# capture environment after all job setup completed
env &> ${UNIFYFS_LOG_DIR}/job.environ
