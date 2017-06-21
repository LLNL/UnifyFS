/*
 * The Self-* Storage System Project
 * Copyright (c) 2004-2011, Carnegie Mellon University.
 * All rights reserved.
 * http://www.pdl.cmu.edu/  (Parallel Data Lab at Carnegie Mellon)
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to reproduce, use, and prepare derivative works of this
 * software is granted provided the copyright and "No Warranty" statements
 * are included with all reproductions and derivative works and associated
 * documentation. This software may also be redistributed without charge
 * provided that the copyright and "No Warranty" statements are included
 * in all redistributions.
 *
 * NO WARRANTY. THIS SOFTWARE IS FURNISHED ON AN "AS IS" BASIS.
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED AS TO THE MATTER INCLUDING, BUT NOT LIMITED
 * TO: WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY
 * OF RESULTS OR RESULTS OBTAINED FROM USE OF THIS SOFTWARE. CARNEGIE
 * MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT
 * TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * COPYRIGHT HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE
 * OR DOCUMENTATION.
 */

/*
 * mlog.h  define API for multilog message logging system
 * 08-Apr-2004  chuck@ece.cmu.edu
 */

#ifndef _MLOG_H_
#define _MLOG_H_

/*
 * mlog flag values
 */
#define MLOG_STDERR     0x80000000      /* always log to stderr */
#define MLOG_UCON_ON    0x40000000      /* enable UDP console on mlog_open */
#define MLOG_UCON_ENV   0x20000000      /* get UCON list from $MLOG_UCON */
#define MLOG_SYSLOG     0x10000000      /* syslog(3) the messages as well */
#define MLOG_LOGPID     0x08000000      /* include pid in log tag */
#define MLOG_FQDN       0x04000000      /* log fully quallified domain name */
#define MLOG_STDOUT     0x02000000      /* always log to stdout */
/* spare bits: 0x01000000-0x00800000 */
#define MLOG_PRIMASK    0x007f0000      /* priority mask */
#define MLOG_EMERG      0x00700000      /* emergency */
#define MLOG_ALERT      0x00600000      /* alert */
#define MLOG_CRIT       0x00500000      /* critical */
#define MLOG_ERR        0x00400000      /* error */
#define MLOG_WARN       0x00300000      /* warning */
#define MLOG_NOTE       0x00200000      /* notice */
#define MLOG_INFO       0x00100000      /* info */
#define MLOG_PRISHIFT   20              /* to get non-debug level */
#define MLOG_DPRISHIFT  16              /* to get debug level */
#define MLOG_DBG        0x000f0000      /* all debug streams */
#define MLOG_DBG0       0x00080000      /* debug stream 0 */
#define MLOG_DBG1       0x00040000      /* debug stream 1 */
#define MLOG_DBG2       0x00020000      /* debug stream 2 */
#define MLOG_DBG3       0x00010000      /* debug stream 3 */
#define MLOG_FACMASK    0x0000ffff      /* facility mask */

/*
 * structures: not really part of the external API, but exposed here
 * so we can do the priority filter (mlog_filter) in a macro before
 * calling mlog() ... no point evaluating the mlog() args if the
 * filter is going to filter log out...
 */

/**
 * mlog_fac: facility name and mask info
 */
struct mlog_fac {
    int fac_mask;        /*!< log level for this facility */
    char *fac_aname;     /*!< abbreviated name of this facility [malloced] */
    char *fac_lname;     /*!< optional long name of this facility [malloced] */
};

/**
 * mlog_xstate: exposed global state... just enough for a level check
 */
struct mlog_xstate {
    char *tag;                       /*!< tag string [malloced] */
    /* note that tag is NULL if mlog is not open/inited */
    struct mlog_fac *mlog_facs;      /*!< array of facility info [malloced] */
    int fac_cnt;                     /*!< # of facilities we are using */
    char *nodename;                  /*!< pointer to our utsname */
};


#if defined(__cplusplus)
extern "C" {   /* __BEGIN_DECLS */
#endif

    /*
     * API prototypes and inlines
     */

    /**
     * mlog_filter: determine if we should log a message, based on its priority
     * and the current mask level for the facility.   flags is typically a
     * constant, so the C optimizer should be able to reduce this inline
     * quite a bit.  no locking for threaded environment here, as we assume
     * fac_cnt can only grow larger and threaded apps will shutdown threads
     * before doing a mlog_close.
     *
     * @param flags the MLOG flags
     * @return 1 if we should log, 0 if we should filter
     */
    static inline int mlog_filter(int flags)
    {
        extern struct mlog_xstate mlog_xst;
        unsigned int fac, lvl, mask;
        /* first, ensure mlog is open */
        if (!mlog_xst.tag) {
            return(0);
        }
        /* get the facility and level of this log message */
        fac = flags & MLOG_FACMASK;
        lvl = flags & MLOG_PRIMASK;
        /*
         * check for valid facility.  if it is not valid, then we convert
         * it to the default facility because that seems like a better thing
         * to do than drop the message.
         */
        if (fac >= (unsigned)mlog_xst.fac_cnt) {
            fac = 0;    /* 0 == default facility */
        }
        /* now we can get the mask we need */
        mask = mlog_xst.mlog_facs[fac].fac_mask;
        /* for non-debug logs we directly compare the mask and level */
        if (lvl >= MLOG_INFO) {
            return( (lvl < mask) ? 0 : 1);
        }
        /*
         * for debugging logs, we check the channel mask bits.  applications
         * that don't use debugging channels always log with all the bits set.
         */
        return( (lvl & mask) == 0 ? 0 : 1);
    }

    /* XXXCDC: BEGIN TMP */

    /*
     * This is here temporarily because some of the code still calls the old
     * plfs_debug. This routine now tranforms the plfs_debug call into an
     * mlog call.
     */

    void plfs_debug(const char *fmt, ...);

    /* XXXCDC: END TMP */


    /**
     * mlog: multilog a message... generic wrapper for the the core vmlog
     * function.  note that a log line cannot be larger than MLOG_TBSZ (4096)
     * [if it is larger it will be (silently) truncated].   facility should
     * allocated with mlog_open(), mlog_namefacility(), mlog_allocfacility(),
     * or mlog_setlogmask() before being used (or the logs will get converted
     * to the default facility, #0).
     *
     * @param flags facility+level+misc flags
     * @param fmt printf-style format string
     * @param ... printf-style args
     */
    void mlog(int flags, const char *fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)));

    /**
     * mlog_abort: like mlog, but does an abort after processing the log.
     * for aborts, we always log to STDERR.
     *
     * @param flags facility+level+misc flags
     * @param fmt printf-style format string
     * @param ... printf-style args
     */
    void mlog_abort(int flags, const char *fmt, ...)
    __attribute__((__noreturn__, __format__(__printf__, 2, 3)));

    /**
     * mlog_abort_hook: establish an abort "hook" to call before doing
     * an abort (e.g. a hook to print the stack, or save some debug info).
     *
     * @param hook the abort hook function
     * @return the old hook (NULL if there wasn't one)
     */
    void *mlog_abort_hook(void (*abort_hook)(void));

    /**
     * mlog_allocfacility: allocate a new facility with the given name
     *
     * @param aname the abbr. name for the facility - can be null for no name
     * @param lname the long name for the new facility - can be null for no name
     * @return new facility number on success, -1 on error - malloc problem.
     */
    int mlog_allocfacility(char *aname, char *lname);

    /**
     * mlog_close: close off an mlog and release any allocated resources.
     * if already close, this function is a noop.
     */
    void mlog_close(void);

    /**
     * mlog_dmesg: obtain pointers to the current contents of the message
     * buffer.   since the message buffer is circular, the result may come
     * back in two pieces.  note that this function returns pointers into
     * the live message buffer, so the app had best not call mlog again
     * until it is done with the pointers.
     *
     * @param b1p returns pointer to first buffer here
     * @param b1len returns length of data in b1
     * @param b2p returns pointer to second buffer here (null if none)
     * @param b2len returns length of b2 or zero if b2 is null
     * @return 0 on success, -1 on error (not open, or no message buffer)
     */
    int mlog_dmesg(char **b1p, int *b1len, char **b2p, int *b2len);

    /**
     * mlog_mbcount: give hint as to current size of message buffer.
     * (buffer size may change if mlog is called after this...)
     *
     * @return number of bytes in msg buffer (zero if empty/disabled)
     */
    int mlog_mbcount(void);

    /**
     * mlog_mbcopy: safely copy the most recent bytes of the message buffer
     * over into another buffer for use.
     *
     * @param buf buffer to copy to
     * @param offset offset in message buffer (0 to start at the end)
     * @param len length of the buffer
     * @return number of bytes copied (<= len), or -1 on error
     */
    int mlog_mbcopy(char *buf, int offset, int len);

    /**
     * mlog_exit: like mlog, but exits with the given status after processing
     * the log.   we always log to STDERR.
     *
     * @param status the value to exit with
     * @param flags facility+level+misc flags
     * @param fmt printf-style format string
     * @param ... printf-style args
     */
    void mlog_exit(int status, int flags, const char *fmt, ...)
    __attribute__((__noreturn__, __format__(__printf__, 3, 4)));

    /**
     * mlog_findmesgbuf: search for a message buffer inside another buffer
     * (typically a mmaped core file).
     *
     * @param b the buffer to search
     * @param len the length of the buffer b
     * @param b1p returns pointer to first buffer here
     * @param b1l returns length of data in b1
     * @param b2p returns pointer to second buffer here (null if none)
     * @param b2l returns length of b2 or zero if b2 is null
     * @return 0 on success, -1 on error
     */
    int mlog_findmesgbuf(char *b, int len, char **b1p, int *b1l,
                         char **b2p, int *b2l);

    /**
     * mlog_namefacility: assign a name to a facility.   since the facility
     * number is used as an index into an array, don't choose large numbers.
     *
     * @param facility the facility to name
     * @param aname the new abbreviated name, or null to remove the name
     * @param lname optional long name (null if not needed)
     * @return 0 on success, -1 on error (malloc problem).
     */
    int mlog_namefacility(int facility, char *aname, char *lname);

    /**
     * mlog_open: open a multilog (uses malloc).  you can only have one
     * multilog open at a time, but you can use multiple facilities.
     * if an mlog is already open, then this call will fail.  if you use
     * the message buffer, you will prob want it to be 1K or larger.
     *
     * @param tag string we tag each line with, optionally followed by pid
     * @param maxfac_hint hint as to largest user fac value that will be used
     * @param default_mask the default mask to use for each facility
     * @param stderr_mask messages with a mask above this go to stderr.  If
     *        this is 0, then output goes to stderr only if MLOG_STDERR is used
     *        (either in mlog_open or in mlog).
     * @param logfile log file name, or null if no log file
     * @param msgbuf_len size of message buffer, or zero if no message buffer
     * @param flags STDERR, UCON_ON, UCON_ENV, SYSLOG, LOGPID
     * @param syslogfac facility to use if MLOG_SYSLOG is set in flags
     * @return 0 on success, -1 on error.
     */
    int mlog_open(char *tag, int maxfac_hint, int default_mask, int stderr_mask,
                  char *logfile, int msgbuf_len, int flags, int syslogfac);

    /**
     * mlog_str2pri: convert a priority string to an int pri value to allow
     * for more user-friendly programs.
     *
     * @param pstr the priority string
     * @return -1 (an invalid pri) on error.
     */
    int mlog_str2pri(const char *pstr);

    /**
     * mlog_reopen: reopen a multilog.  this will reopen the log file,
     * reset the ucon socket (if on), and refresh the pid in the tag (if
     * present).   call this to rotate log files or after a fork (which
     * changes the pid and may also close fds).    the logfile param should
     * be set to a zero-length string ("") to keep the old value of logfile.
     * if the logfile is NULL, then any open logfiles will be switched off.
     * if the logfile is a non-zero length string, it is the new logfile name.
     *
     * @param logfile settings for the logfile after reopen (see above).
     * @return 0 on success, -1 on error.
     */
    int mlog_reopen(char *logfile);

    /**
     * mlog_setlogmask: set the logmask for a given facility.  if the user
     * uses a new facility, we ensure that our facility array covers it
     * (expanding as needed).
     *
     * @param facility the facility we are adjusting (16 bit int)
     * @param mask the new mask to apply
     * @return the old mask val, or -1 on error.  cannot fail if facility array
     * was preallocated.
     */
    int mlog_setlogmask(int facility, int mask);

    /**
     * mlog_setmasks: set mlog masks for a set of facilities to a given level.
     * the input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
     * where the "PREFIX" is the facility name defined with mlog_namefacility().
     *
     * @param mstr settings to use (doesn't have to be null term'd if mstr >= 0)
     * @param mlen length of mstr (if < 0, assume null terminated, use strlen)
     */
    void mlog_setmasks(char *mstr, int mlen);

    /**
     * mlog_getmasks: get current mask level as a string (not null terminated).
     * if the buffer is null, we probe for length rather than fill.
     *
     * @param buf the buffer to put the results in (NULL == probe for length)
     * @param discard bytes to discard before starting to fill buf (normally 0)
     * @param len length of the buffer
     * @param unterm if non-zero do not include a trailing null
     * @return bytes returned (may be trunced and non-null terminated if == len)
     */
    int mlog_getmasks(char *buf, int discard, int len, int unterm);

    /**
     * mlog_ucon_add: add an endpoint as a ucon
     *
     * @param host hostname (or IP) of remote endpoint
     * @param port udp port (in host byte order)
     * @return 0 on success, -1 on error
     */
    int mlog_ucon_add(char *host, int port);

    /**
     * mlog_ucon_on: enable ucon (UDP console)
     *
     * @return 0 on success, -1 on error
     */
    int mlog_ucon_on(void);

    /**
     * mlog_ucon_off: disable ucon (UDP console) if enabled
     *
     * @return 0 on success, -1 on error
     */
    int mlog_ucon_off(void);

    /**
     * mlog_ucon_rm: remove an ucon endpoint
     *
     * @param host hostname (or IP) of remote endpoint
     * @param port udp port (in host byte order)
     * @return 0 on success, -1 on error
     */
    int mlog_ucon_rm(char *host, int port);


#ifndef MLOG_NOMACRO_OPT
    /*
     * here's some cpp-based optimizations
     */

    /*
     * use -DMLOG_NEVERLOG=1 to compile out all the mlog calls (e.g. for
     * performance testing, when you want to get rid of all extra overheads).
     */
#ifndef MLOG_NEVERLOG
#define MLOG_NEVERLOG 0      /* default value is to keep mlog */
#endif

    /*
     * turn log into a macro so that we can check the log level before
     * evaluating all the mlog() args.   no point in computing the args
     * and building a call stack if we are not going to do anything.
     *
     * you can't do this with inline functions, because gcc will not
     * inline functions that use "..." so doing something like:
     *  void inline mlog(int f, char *c, ...) { return; }
     * and then
     *  mlog(MLOG_INFO, "fputs=%d", fputs("fputs was called", stdout));
     * will not inline out the function call setup, so fputs() will
     * get called even though mlog doesn't do anything.
     *
     * vmlog() will refilter, but it also has to handle stderr_mask, so
     * it isn't a big deal to have it recheck the level... could add
     * a flag to tell vmlog() to skip the filter if it was an issue.
     *
     * this assumes your cpp supports "..." and __VA_ARGS__ (gcc does).
     *
     * note that cpp does not expand the mlog() call inside the #define,
     * that goes to the real mlog function.
     */

#define mlog(LEVEL, ...) do {                                       \
    if (MLOG_NEVERLOG == 0 && mlog_filter(LEVEL))                   \
        mlog((LEVEL), __VA_ARGS__);                                 \
    } while (0)

#endif /* MLOG_NOMACRO_OPT */


#if defined(__cplusplus)
} /* __END_DECLS */
#endif

#endif  /* _MLOG_H_ */
