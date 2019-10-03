#
#  Project-local sharness code for UnifyFS
#

# Run command with a timeout
#
# $1        - Number of seconds to wait before timing out
# $2 .. $n  - Command and arguments to execute
run_timeout()
{
    perl -e 'alarm shift @ARGV; exec @ARGV' "$@"
}

# Check if a process with a given name is running, retrying up
# to a given number of seconds before giving up.
#
# $1 - Name of a process to check for
# $2 - Number of seconds to wait before giving up
#
# Returns 0 if the named process is found, otherwise returns 1.
process_is_running()
{
    local proc=${1:-"unifyfsd"}
    local secs_to_wait=${2:-15}
    local max_loops=$(($secs_to_wait * 2))
    local i=0

    while test "$i" -le "$max_loops"; do
        if ! test -z "$(pidof $proc)" ; then
            return 0
        else
            sleep .5
        fi
        i=$(($i + 1))
    done
    return 1
}

# Check if a process with a given name is not running, retrying up
# to a given number of seconds before giving up.
#
# $1 - Name of a process to check for
# $2 - Number of seconds to wait before giving up
#
# Returns 0 if the named process is not found, otherwise returns 1.
process_is_not_running()
{
    local proc=${1:-"unifyfsd"}
    local secs_to_wait=${2:-15}
    local max_loops=$(($secs_to_wait * 2))
    local i=0

    while test "$i" -le "$max_loops"; do
        if test -z "$(pidof $proc)" ; then
            return 0
        else
            sleep .5
        fi
        i=$(($i + 1))
    done
    return 1
}

# Create metadata directory if needed and start daemon.
unifyfsd_start_daemon()
{
    # Make sure metadata directory exists
    if test -z "$UNIFYFS_META_DB_PATH"; then
        return 1
    elif ! test -d "$UNIFYFS_META_DB_PATH" &&
         ! mkdir $UNIFYFS_META_DB_PATH; then
        return 1
    fi

    # Generate servers hostfile
    # if test -z "$UNIFYFS_SHAREDFS_DIR"; then
    #     return 1
    # elif ! test -d "$UNIFYFS_SHAREDFS_DIR" &&
    #      ! mkdir $UNIFYFS_SHAREDFS_DIR; then
    #     return 1
    # fi
    # srvr_hosts=$UNIFYFS_SHAREDFS_DIR/unifyfsd.hosts
    # if [ ! -f $srvr_hosts ]; then
    #     touch $srvr_hosts
    #     echo "1" >> $srvr_hosts
    #     hostname >> $srvr_hosts
    # fi
    # export UNIFYFS_SERVER_HOSTFILE=$srvr_hosts

    # run server daemon
    $UNIFYFSD
}

# Kill UnifyFS daemon.
unifyfsd_stop_daemon()
{
    while killall -q -s TERM unifyfsd 2>/dev/null; do :; done
}

# Remove the metadata directory.
unifyfsd_cleanup()
{
    test -d "$UNIFYFS_META_DB_PATH" && rm -rf $UNIFYFS_META_DB_PATH
    # test -d "$UNIFYFS_SHAREDFS_DIR" && rm -rf $UNIFYFS_SHAREDFS_DIR
}
