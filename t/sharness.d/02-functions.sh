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

# Dump test state (for debugging)
unifyfsd_dump_state()
{
    if ! test -d "$UNIFYFS_TEST_TMPDIR"; then
        return 1
    fi

    dumpfile=$UNIFYFS_TEST_TMPDIR/unifyfsd.dump.$$
    [ -f $dumpfile ] || touch $dumpfile

    metadir=$UNIFYFS_TEST_META
    if [ -d $metadir ]; then
        echo "Listing meta directory $metadir :" >> $dumpfile
        ls -lR $metadir >> $dumpfile
        echo >> $dumpfile
    fi

    sharedir=$UNIFYFS_TEST_SHARE
    if [ -d $sharedir ]; then
        echo "Listing share directory $sharedir :" >> $dumpfile
        ls -lR $sharedir >> $dumpfile
        echo >> $dumpfile
    fi

    spilldir=$UNIFYFS_TEST_SPILL
    if [ -d $spilldir ]; then
        echo "Listing spill directory $spilldir :" >> $dumpfile
        ls -lR $spilldir >> $dumpfile
        echo >> $dumpfile
    fi

    statedir=$UNIFYFS_TEST_STATE
    if [ -d $statedir ]; then
        echo "Listing state directory $statedir :" >> $dumpfile
        ls -lR $statedir >> $dumpfile
        echo >> $dumpfile
        echo "Dumping state directory $statedir file contents :" >> $dumpfile
        for f in $statedir/* ; do
            if [ -f $f ]; then
                echo "========= $f ==========" >> $dumpfile
                cat $f >> $dumpfile
                echo "+++++++++++++++++++++++" >> $dumpfile
                echo >> $dumpfile
            fi
        done
    fi

    server_log=$UNIFYFS_TEST_TMPDIR/unifyfsd.stdlog
    if [ -f $server_log ]; then
        echo "Dumping server stdout/err file contents :" >> $dumpfile
        echo "========= $server_log ==========" >> $dumpfile
        cat $server_log >> $dumpfile
        echo "+++++++++++++++++++++++" >> $dumpfile
        echo >> $dumpfile
    fi

    # copy dumpfile to /tmp for later access
    cp $dumpfile /tmp

    return 0
}

# Remove the test directory.
unifyfsd_cleanup()
{
    unifyfsd_dump_state
    # remove test directory if it exists
    test -d "$UNIFYFS_TEST_TMPDIR" && /bin/rm -r $UNIFYFS_TEST_TMPDIR
    return 0
}

# Create metadata directory if needed and start daemon.
unifyfsd_start_daemon()
{
    # Make sure test directory exists
    if ! test -d "$UNIFYFS_TEST_TMPDIR"; then
        return 1
    fi

    # Generate servers hostfile
    if test -z "$UNIFYFS_SHAREDFS_DIR"; then
        return 1
    fi
    srvr_hosts=$UNIFYFS_SHAREDFS_DIR/unifyfsd.hosts
    if [ ! -f $srvr_hosts ]; then
        touch $srvr_hosts
        echo "1" >> $srvr_hosts
        hostname >> $srvr_hosts
    fi
    export UNIFYFS_SERVER_HOSTFILE=$srvr_hosts

    # run server daemon as background process
    $UNIFYFSD >$UNIFYFS_TEST_TMPDIR/unifyfsd.stdlog 2>&1 &
}

# Kill UnifyFS daemon.
unifyfsd_stop_daemon()
{
    killsig="TERM"
    srvrpids="$(pgrep unifyfsd)"
    while [ -n "$srvrpids" ]; do
        pkill --signal $killsig unifyfsd 2>/dev/null
        sleep 5
        srvrpids="$(pgrep unifyfsd)"
        killsig="KILL"
    done
}


