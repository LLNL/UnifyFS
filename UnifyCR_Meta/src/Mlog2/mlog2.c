/*
 * The Self-* Storage System Project
 * Copyright (c) 2004, Carnegie Mellon University.
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
 * mlog.c: multilog message logging system
 * 08-Apr-2004  chuck@ece.cmu.edu
 */

#define MLOG_MUTEX

#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    /* for strncasecmp */
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifdef MLOG_MUTEX
#include <pthread.h>
#endif

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "mlog2.h"


/*
 * dispose of the mlog() macro function, if it is defined.   we need
 * the real thing here...
 */
#ifdef mlog
#undef mlog
#endif /* mlog */

#define MLOG_TAGPAD 16  /* extra tag bytes to alloc for a pid */

/**
 * message buffer header: lives at the start of a message buffer, is
 * malloced with it, and contains pointers + metainfo.
 */
struct mlog_mbhead {
#define MBH_START ">CpMdUl<"
    char mbh_start[8];  /*!< magic string that marks start of msgbuf */
    uint32_t mbh_beef;  /*!< 0xdeadbeef, for checking byte order */
    uint32_t mbh_len;   /*!< length of buffer (not including header) */
    uint32_t mbh_cnt;   /*!< number of bytes currently in buffer */
    uint32_t mbh_wp;    /*!< write pointer */
};

/**
 * internal global state
 */
struct mlog_state {
    /* note: tag, mlog_facs, and fac_cnt are in xstate now */

    int def_mask;                   /*!< default facility mask value */
    int stderr_mask;                /*!< mask above which we send to stderr  */
    char *logfile;                  /*!< logfile name [malloced] */
    int logfd;                      /*!< fd of the open logfile */
    int oflags;                     /*!< open flags */
    int fac_alloc;                  /*!< # of slots in facs[] (>=fac_cnt) */
    unsigned char *mb;              /*!< message buffer [malloced] */
    int udpsock;                    /*!< udp socket for cons output */
    int ucon_cnt;                   /*!< number of UDP output targets */
    int ucon_nslots;                /*!< # of malloc'd entries in ucons[] */
    struct sockaddr_in *ucons;      /*!< UDP output targets [malloced] */
    struct utsname uts;             /*!< for hostname, from uname(3) */
    void (*abort_hook)(void);       /*!< abort hook for mlog_abort() */
    int stdout_isatty;              /*!< non-zero if stdout is a tty */
    int stderr_isatty;              /*!< non-zero if stderr is a tty */
#ifdef MLOG_MUTEX
    pthread_mutex_t mlogmux;        /*!< protect mlog in threaded env */
#endif
};

/*
 * global data.  this sets mlog_xst.tag to 0, meaning the log is not open.
 * this is global so mlog_filter() in mlog.h can get at it.
 */
struct mlog_xstate mlog_xst = { 0 };

/*
 * static data.
 */
static struct mlog_state mst = { 0 };
static int mlogsyslog[] = {
    LOG_DEBUG,     /* MLOG_DBUG */
    LOG_INFO,      /* MLOG_INFO */
    LOG_NOTICE,    /* MLOG_NOTE */
    LOG_WARNING,   /* MLOG_WARN */
    LOG_ERR,       /* MLOG_ERR  */
    LOG_CRIT,      /* MLOG_CRIT */
    LOG_ALERT,     /* MLOG_ALERT */
    LOG_EMERG,     /* MLOG_EMERG */
};
static const char *default_fac0name = "MLOG";   /* default name for facility 0 */

/*
 * macros
 */
#ifdef MLOG_MUTEX
#define mlog_lock()   pthread_mutex_lock(&mst.mlogmux)
#define mlog_unlock() pthread_mutex_unlock(&mst.mlogmux)
#else
#define mlog_lock()   /* nothing */
#define mlog_unlock() /* nothing */
#endif

/*
 * local prototypes
 */
static void mlog_getmbptrs(char **, int *, char **, int *);
static void mlog_dmesg_mbuf(char **b1p, int *b1len, char **b2p, int *b2len);
static int mlog_getucon(int, struct sockaddr_in *, char *);
static const char *mlog_pristr(int);
static int mlog_resolvhost(struct sockaddr_in *, char *, char *);
static int mlog_setnfac(int);
static uint32_t wswap(uint32_t);
static void vmlog(int, const char *, va_list);

/*
 * local helper functions
 */

/**
 * mlog_getmbptrs: get pointers to the message buffer and their sizes,
 * based on the current value of the write pointer.   if the write pointer
 * is zero, then the entire buffer is in one and twolen is set to zero.
 * note that the _caller_ must check that mst.mb is valid, we assume it is.
 * caller must hold mlog_lock.
 *
 * @param one pointer to first part of circular buffer
 * @param onelen length of the one buffer
 * @param two pointer to second part of circular buffer, if any
 * @param twolen length of the two buffer
 */
static void mlog_getmbptrs(char **one, int *onelen, char **two, int *twolen)
{
    uint32_t wp;
    wp = ((struct mlog_mbhead *)mst.mb)->mbh_wp;
    *one = ((char *) mst.mb) + sizeof(struct mlog_mbhead) + wp;
    *onelen = ((struct mlog_mbhead *)mst.mb)->mbh_len - wp;
    *two = ((char *) mst.mb) + sizeof(struct mlog_mbhead);
    *twolen = wp;
}

/**
 * mlog_dmesg_mbuf: obtain pointers to the current contents of the
 * message buffer.  since the message buffer is circular, the result
 * may come back in two pieces.
 * caller must hold mlog_lock.
 *
 * @param b1p returns pointer to first buffer here
 * @param b1len returns length of data in b1
 * @param b2p returns pointer to second buffer here (null if none)
 * @param b2len returns length of b2 or zero if b2 is null
 */
static void mlog_dmesg_mbuf(char **b1p, int *b1len, char **b2p, int *b2len)
{
    uint32_t skip;
    /* get pointers */
    mlog_getmbptrs(b1p, b1len, b2p, b2len);
    /* if the buffer wasn't full, we need to adjust the pointers */
    skip = ((struct mlog_mbhead *)mst.mb)->mbh_len -
           ((struct mlog_mbhead *)mst.mb)->mbh_cnt;
    if (skip >= *b1len) {       /* skip entire first buffer? */
        skip -= *b1len;
        *b1p = *b2p;
        *b2p = 0;
        *b1len = *b2len;
        *b2len = 0;
    }
    if (skip) {
        *b1p = *b1p + skip;
        *b1len = *b1len - skip;
    }
    return;
}

/**
 * mlog_getucon: helper function that parses hostname/port info from
 * a string into an array of sockaddr_in structures.  the string format
 * is:   host1:port1;host2:port2;host3:port3 ...
 * does not access global mlog state.
 *
 * @param cnt number of sockaddr_in structures alloced for ads[] array
 * @param ads an array of sockaddr_in structures we fill in
 * @param dcon text string to load ucon info from
 * @return number of host/port entries resolved.
 */
static int mlog_getucon(int cnt, struct sockaddr_in *ads, char *dcon)
{
    int rv;
    char *p, *hst, *col, *port;
    p = dcon;
    rv = 0;
    while (*p && rv < cnt) {
        hst = p;
        while (*p && *p != ':') {
            p++;
        }
        if (*p != ':') {
            fprintf(stderr, "MLOG_UCON: parse error: missing ':'\n");
            break;
        }
        col = p++;
        port = p;
        while (*p && *p != ';') {
            p++;
        }
        if (*p == ';') {
            p++;
        }
        *col = 0;
        if (mlog_resolvhost(&ads[rv], hst, port) != -1) {
            rv++;
        }
        *col = ':';
    }
    return(rv);
}

/*
 * static arrays for converting between pri's and strings
 */
static const char *norm[] = { "DBUG", "INFO", "NOTE", "WARN",
                              "ERR ", "CRIT", "ALRT", "EMRG"
                            };
static const char *dbg[] = {  "D---", "D3--", "D2--", "D23-",
                              "D1--", "D13-", "D12-", "D123",
                              "D0--", "D03-", "D02-", "D023",
                              "D01-", "D013", "D012", "DBUG"
                           };
/**
 * mlog_pristr: convert priority to 4 byte symbolic name.
 * does not access mlog global state.
 *
 * @param pri the priority to convert to a string
 * @return the string (symbolic name) of the priority
 */
static const char *mlog_pristr(int pri)
{
    int s;
    pri = pri & MLOG_PRIMASK;   /* be careful */
    s = (pri >> MLOG_PRISHIFT) & 7;
    if (s) {
        return(norm[s]);
    }
    s = (pri >> MLOG_DPRISHIFT) & 15;
    return(dbg[s]);
}

/**
 * mlog_resolvhost: another helper function that does hostname resolution.
 * does not access mlog global state.
 *
 * @param sinp pointer to sockaddr_in that we fill in based on h/p
 * @param h hostname to resolve; also handles IP address
 * @param p port number
 * @return -1 on error, 0 otherwise.
 */
static int mlog_resolvhost(struct sockaddr_in *sinp, char *h, char *p)
{
    struct hostent *he;
    memset(sinp, 0, sizeof(*sinp));
    sinp->sin_family = AF_INET;
    sinp->sin_port = htons(atoi(p));
    if (*h >= '0' && *h <= '9') {
        sinp->sin_addr.s_addr = inet_addr(h);
        if (sinp->sin_addr.s_addr == 0 ||
                sinp->sin_addr.s_addr == ((in_addr_t) -1)) {
            fprintf(stderr, "MLOG_UCON: invalid host %s\n", h);
            return(-1);
        }
    } else {
        he = gethostbyname(h);   /* likely to block here */
        if (!he || he->h_addrtype != AF_INET ||
                he->h_length != sizeof(in_addr_t) || !he->h_addr) {
            fprintf(stderr, "MLOG_UCON: invalid host %s\n", h);
            return(-1);
        }
        memcpy(&sinp->sin_addr.s_addr, he->h_addr, he->h_length);
    }
    return(0);
}

/**
 * mlog_setnfac: set the number of facilites allocated (including default
 * to a given value).   mlog must be open for this to do anything.
 * we set the default name for facility 0 here.
 * caller must hold mlog_lock.
 *
 * @param n the number of facilities to allocate space for now.
 * @return -1 on error.
 */
static int mlog_setnfac(int n)
{
    int try, lcv;
    struct mlog_fac *nfacs;

    /*
     * no need to check mlog_xst.tag to see if mlog is open or not,
     * since caller holds mlog_lock already it must be ok.
     */

    /* hmm, already done */
    if (n <= mlog_xst.fac_cnt) {
        return(0);
    }
    /* can we expand in place? */
    if (n <= mst.fac_alloc) {
        mlog_xst.fac_cnt = n;
        return(0);
    }
    /* must grow the array */
    try = (n < 1024) ? (n + 32) : n;    /* pad a bit for small values of n */
    nfacs = calloc(1, try * sizeof(*nfacs));
    if (!nfacs) {
        return(-1);
    }
    /* over the hump, setup the new array */
    lcv = 0;
    if (mlog_xst.mlog_facs && mlog_xst.fac_cnt) {   /* copy old? */
        for (/*null*/ ; lcv < mlog_xst.fac_cnt ; lcv++) {
            nfacs[lcv] = mlog_xst.mlog_facs[lcv];    /* struct copy */
        }
    }
    for (/*null*/ ; lcv < try ; lcv++) {           /* init the new */
            nfacs[lcv].fac_mask = mst.def_mask;
            nfacs[lcv].fac_aname = (lcv == 0) ? (char *)default_fac0name : NULL;
            nfacs[lcv].fac_lname = NULL;
        }
    /* install */
    if (mlog_xst.mlog_facs) {
        free(mlog_xst.mlog_facs);
    }
    mlog_xst.mlog_facs = nfacs;
    mlog_xst.fac_cnt = n;
    mst.fac_alloc = try;
    return(0);
}

/**
 * mlog_bput: copy a string to a buffer, counting the bytes
 *
 * @param bpp pointer to output pointer (we advance it)
 * @param skippy pointer to bytes to skip
 * @param residp pointer to length of buffer remaining
 * @param totcp pointer to total bytes moved counter
 * @param str the string to copy in (null to just add a \0)
 */
static void mlog_bput(char **bpp, int *skippy, int *residp, int *totcp,
                      const char *str)
{
    static const char *nullsrc = "X\0\0";          /* 'X' is a non-null dummy char */
    const char *sp;
    if (str == NULL) {                       /* trick to allow a null insert */
        str = nullsrc;
    }
    for (sp = str ; *sp ; sp++) {
        if (sp == nullsrc) {
            sp++;    /* skip over 'X' to null */
        }
        if (totcp) {
            (*totcp)++;                      /* update the total */
        }
        if (skippy && *skippy > 0) {
            (*skippy)--;                     /* honor skip */
            continue;
        }
        if (*residp > 0 && *bpp != NULL) {   /* copyout if buffer w/space */
            **bpp = *sp;
            (*bpp)++;
            (*residp)--;
        }
    }
    return;
}

/**
 * wswap: swap byte order of a 32bit word
 * does not access mlog global state.
 *
 * @param w the 32bit int to swap
 * @return the swapped version of the "w"
 */
static uint32_t wswap(uint32_t w)
{
    return( (w >> 24) |
            ((w >> 16) & 0xff) << 8  |
            ((w >>  8) & 0xff) << 16 |
            ((w      ) & 0xff) << 24 );
}

/**
 * mlog_cleanout: release previously allocated resources (e.g. from a
 * close or during a failed open).  this function assumes the mlogmux
 * has been allocated (caller must ensure that this is true or we'll
 * die when attempting a mlog_lock()).  we will dispose of mlogmux.
 * (XXX: might want to switch over to a PTHREAD_MUTEX_INITIALIZER for
 * mlogmux at some point?).
 *
 * the caller handles cleanout of mlog_xst.tag (not us).
 */
static void mlog_cleanout()
{
    int lcv;
    mlog_lock();
    if (mst.logfile) {
        if (mst.logfd >= 0) {
            close(mst.logfd);
        }
        mst.logfd = -1;
        free(mst.logfile);
        mst.logfile = NULL;
    }
    if (mlog_xst.mlog_facs) {
        /*
         * free malloced facility names, being careful not to free
         * the static default_fac0name....
         */
        for (lcv = 0 ; lcv < mst.fac_alloc ; lcv++) {
            if (mlog_xst.mlog_facs[lcv].fac_aname &&
                    mlog_xst.mlog_facs[lcv].fac_aname != default_fac0name) {
                free(mlog_xst.mlog_facs[lcv].fac_aname);
            }
            if (mlog_xst.mlog_facs[lcv].fac_lname) {
                free(mlog_xst.mlog_facs[lcv].fac_lname);
            }
        }
        free(mlog_xst.mlog_facs);
        mlog_xst.mlog_facs = NULL;
        mlog_xst.fac_cnt = mst.fac_alloc = 0;
    }
    if (mst.mb) {
        free(mst.mb);
        mst.mb = NULL;
    }
    if (mst.udpsock >= 0) {
        close(mst.udpsock);
        mst.udpsock = -1;
    }
    if (mst.ucons) {
        free(mst.ucons);
        mst.ucons = NULL;
    }
    if (mst.oflags & MLOG_SYSLOG) {
        closelog();
    }
    mlog_unlock();
#ifdef MLOG_MUTEX
    pthread_mutex_destroy(&mst.mlogmux);
#endif
}


/**
 * vmlog: core log function, front-ended by mlog/mlog_abort/mlog_exit.
 * we vsnprintf the message into a holding buffer to format it.  then we
 * send it to all target output logs.  the holding buffer is set to
 * MLOG_TBSIZ, if the message is too long it will be silently truncated.
 * caller should not hold mlog_lock, vmlog will grab it as needed.
 *
 * @param flags the flags (mainly fac+pri) for this log message
 * @param fmt the printf(3) format to use
 * @param ap the stdargs va_list to use for the printf format
 */
static void vmlog(int flags, const char *fmt, va_list ap)
{
#define MLOG_TBSIZ    4096    /* bigger than any line should be */
    int fac, lvl, msk;
    char b[MLOG_TBSIZ], *bp, *b_nopt1hdr;
    char facstore[16], *facstr;
    struct timeval tv;
    struct tm *tm;
    int hlen_pt1, hlen, mlen, tlen, thisflag;
    int resid;
    char *m1, *m2;
    int m1len, m2len, ncpy;
    //since we ignore any potential errors in MLOG let's always re-set 
    //errno to its orginal value
    int save_errno = errno;
    struct mlog_mbhead *mb;
    /*
     * make sure the mlog is open
     */
    if (!mlog_xst.tag) {
        return;
    }
    /*
     * first, see if we can ignore the log messages because it is
     * masked out.  if debug messages are masked out, then we just
     * directly compare levels.  if debug messages are not masked,
     * then we allow all non-debug messages and for debug messages we
     * check to make sure the proper bit is on.  [apps that don't use
     * the debug bits just log with MLOG_DBG which has them all set]
     */
    fac = flags & MLOG_FACMASK;
    lvl = flags & MLOG_PRIMASK;
    /* convert unknown facilities to default so we don't drop log msg */
    if (fac >= mlog_xst.fac_cnt) {
        fac = 0;
    }
    msk = mlog_xst.mlog_facs[fac].fac_mask;
    if (lvl >= MLOG_INFO) {   /* normal mlog message */
        if (lvl < msk) {
            errno = save_errno;
            return;    /* skip it */
        }
        if (mst.stderr_mask != 0 && lvl >= mst.stderr_mask) {
            flags |= MLOG_STDERR;
        }
    } else {                  /* debug mlog message */
        /*
         * note: if (msk >= MLOG_INFO), then all the mask's debug bits
         * are zero (meaning debugging messages are masked out).  thus,
         * for messages with the debug level we only have to do a bit
         * test.
         */
        if ((lvl & msk) == 0) { /* do we want this type of debug msg? */
            errno = save_errno;
            return;    /* no! */
        }
        if ((lvl & mst.stderr_mask) != 0) {  /* same thing for stderr_mask */
            flags |= MLOG_STDERR;
        }
    }
    /*
     * we must log it, start computing the parts of the log we'll need.
     */
    mlog_lock();      /* lock out other threads */
    if (mlog_xst.mlog_facs[fac].fac_aname) {
        facstr = mlog_xst.mlog_facs[fac].fac_aname;
    } else {
        snprintf(facstore, sizeof(facstore), "%d", fac);
        facstr = facstore;
    }
    (void) gettimeofday(&tv, 0);
    tm = localtime(&tv.tv_sec);
    thisflag = (mst.oflags | flags);
    /*
     * ok, first, put the header into b[]
     */
    hlen = snprintf(b, sizeof(b),
                    "%04d/%02d/%02d-%02d:%02d:%02d.%02ld %s %s ",
                    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec,
                    (long int)tv.tv_usec / 10000, mst.uts.nodename,
                    mlog_xst.tag);
    hlen_pt1 = hlen;    /* save part 1 length */
    if (hlen < sizeof(b)) {
        hlen += snprintf(b + hlen, sizeof(b) - hlen, "%-4s %s ",
                         facstr, mlog_pristr(lvl));
    }
    /*
     * we expect there is still room (i.e. at least one byte) for a
     * message, so this overflow check should never happen, but let's
     * check for it anyway.
     */
    if (hlen + 1 >= sizeof(b)) {
        mlog_unlock();      /* drop lock, this is the only early exit */
        fprintf(stderr, "mlog: header overflowed %zd byte buffer (%d)\n",
                sizeof(b), hlen + 1);
        errno = save_errno;
        return;
    }
    /*
     * now slap in the user's data at the end of the buffer
     */
    mlen = vsnprintf(b + hlen, sizeof(b) - hlen, fmt, ap);
    /*
     * compute total length, check for overflows...  make sure the string
     * ends in a newline.
     */
    tlen = hlen + mlen;
    /* if overflow or totally full without newline at end ... */
    if (tlen >= sizeof(b) ||
            (tlen == sizeof(b) - 1 && b[sizeof(b)-2] != '\n') ) {
        tlen = sizeof(b) - 1;   /* truncate, counting final null */
        /*
         * could overwrite the end of b with "[truncated...]" or
         * something like that if we wanted to note the problem.
         */
        b[sizeof(b)-2] = '\n';  /* jam a \n at the end */
    } else {
        /* it fit, make sure it ends in newline */
        if (b[tlen - 1] != '\n') {
            b[tlen++] = '\n';
            b[tlen] = 0;
        }
    }
    b_nopt1hdr = b + hlen_pt1;
    /*
     * multilog message is now ready to be dispatched.
     */
    /*
     * 1: log it to the message buffer (note: mlog still locked)
     */
    mb = (struct mlog_mbhead *)mst.mb;
    if (mb) {
        resid = tlen;
        bp = b;
        /* wont fit?   truncate... */
        if (resid > mb->mbh_len) {
            bp = b + resid - mb->mbh_len;
            resid = mb->mbh_len;
        }
        mlog_getmbptrs(&m1, &m1len, &m2, &m2len);
        ncpy = resid;
        if (ncpy > m1len) {
            ncpy = m1len;
        }
        memcpy(m1, bp, ncpy);
        resid -= ncpy;
        if (resid) {
            bp += ncpy;
            memcpy(m2, bp, resid);
        }
        /* update write pointer */
        if (tlen < mb->mbh_len) {
            mb->mbh_wp += tlen;
            if (mb->mbh_wp >= mb->mbh_len) {
                mb->mbh_wp -= mb->mbh_len;
            }
        }
        if (mb->mbh_cnt < mb->mbh_len) {
            mb->mbh_cnt += tlen;
            if (mb->mbh_cnt > mb->mbh_len) {
                mb->mbh_cnt = mb->mbh_len;
            }
        }
    }
    /*
     * locking options: b[] is current an auto var on the stack.
     * this costs stack space, but means we can unlock earlier.
     * (if b[] was static, you'd hold the lock until the end.)
     * clearly the logfile needs mst.logfd to be stable, and the
     * UCON walks the mst.ucons[] array...
     *
     * neither the stderr/out or syslog access parts of mst that
     * change, so we don't really need it locked for that?
     */
    /*
     * 2: log it to the log file
     */
    if (mst.logfd >= 0) {
        (void) write(mst.logfd, b, tlen);
    }
    /*
     * 3: log it to the UCONs (UDP console)  [mst.oflags' MLOG_UCON_ON bit
     *    can only be set if there is a valid mst.udpsock open]
     */
    if (mst.oflags & MLOG_UCON_ON) {
        for (ncpy = 0 ; ncpy < mst.ucon_cnt ; ncpy++)
            (void) sendto(mst.udpsock, b, tlen, 0,
                          (struct sockaddr *)&mst.ucons[ncpy],
                          sizeof(mst.ucons[ncpy]));
    }
    mlog_unlock();   /* drop lock here */
    /*
     * 4: log it to stderr and/or stdout.  skip part one of the header
     * if the output channel is a tty
     */
    if (thisflag & MLOG_STDERR) {
        if (mst.stderr_isatty) {
            fprintf(stderr, "%s", b_nopt1hdr);
        } else {
            fprintf(stderr, "%s", b);
        }
    }
    if (thisflag & MLOG_STDOUT) {
        if (mst.stderr_isatty) {
            printf("%s", b_nopt1hdr);
        } else {
            printf("%s", b);
        }
        fflush(stdout);
    }
    /*
     * 5: log it to syslog
     */
    if (mst.oflags & MLOG_SYSLOG) {
        b[tlen - 1] = 0;     /* syslog doesn't want the \n */
        syslog(mlogsyslog[lvl >> MLOG_PRISHIFT], "%s", b_nopt1hdr);
        b[tlen - 1] = '\n';  /* put \n back, just to be safe */
    }
    /*
     * done!
     */
    errno = save_errno;
    return;
}

/*
 * API functions
 */

/*
 * mlog_str2pri: convert a priority string to an int pri value to allow
 * for more user-friendly programs.  returns -1 (an invalid pri) on error.
 * does not access mlog global state.
 */
int mlog_str2pri(const char *pstr)
{
    char ptmp[8];
    int lcv;
    /* make sure we have a valid input */
    if (strlen(pstr) > 5) {
        return(-1);
    }
    strcpy(ptmp, pstr);     /* because we may overwrite parts of it */
    /*
     * handle some quirks
     */
    if (strcasecmp(ptmp, "ERR") == 0) { /* has trailing space in the array */
        return(MLOG_ERR);
    }
    if (strcasecmp(ptmp, "DEBUG") == 0) { /* 5 char alternative to 'DBUG' */
        return(MLOG_DBG);
    }
    if (ptmp[0] == 'D') {  /* allow shorthand without the '-' chars */
        while (strlen(ptmp) < 4) {
            strcat(ptmp, "-");
        }
    }
    /*
     * do non-debug first, then debug
     */
    for (lcv =  1 ; lcv <= 7 ; lcv++) {
        if (strcasecmp(ptmp, norm[lcv]) == 0) {
            return(lcv << MLOG_PRISHIFT);
        }
    }
    for (lcv = 0 ; lcv < 16 ; lcv++) {
        if (strcasecmp(ptmp, dbg[lcv]) == 0) {
            return(lcv << MLOG_DPRISHIFT);
        }
    }
    /* bogus! */
    return(-1);
}

/*
 * mlog_open: open a multilog (uses malloc, inits global state).  you
 * can only have one multilog open at a time, but you can use multiple
 * facilities.
 *
 * if an mlog is already open, then this call will fail.  if you use
 * the message buffer, you prob want it to be 1K or larger.
 *
 * return 0 on success, -1 on error.
 */
int mlog_open(char *tag, int maxfac_hint, int default_mask, int stderr_mask,
              char *logfile, int msgbuf_len, int flags, int syslogfac)
{
    int tagblen;
    char *newtag, *dcon, *cp;
    struct mlog_mbhead *mb;
    /* quick sanity check (mst.tag is non-null if already open) */
    if (mlog_xst.tag || !tag ||
            (maxfac_hint < 0) || (default_mask & ~MLOG_PRIMASK) ||
            (stderr_mask & ~MLOG_PRIMASK) ||
            (msgbuf_len < 0) || (msgbuf_len > 0 && msgbuf_len < 16)) {
        return(-1);
    }
    /* init working area so we can use mlog_cleanout to bail out */
    memset(&mst, 0, sizeof(mst));
    mst.logfd = mst.udpsock = -1;
    /* start filling it in */
    tagblen = strlen(tag) + MLOG_TAGPAD;     /* add a bit for pid */
    newtag = calloc(1, tagblen);
    if (!newtag) {
        return(-1);
    }
#ifdef MLOG_MUTEX    /* create lock */
    if (pthread_mutex_init(&mst.mlogmux, NULL) != 0) {
        /* XXX: consider cvt to PTHREAD_MUTEX_INITIALIZER */
        free(newtag);
        return(-1);
    }
#endif
    /* it is now safe to use mlog_cleanout() for error handling */
    
    mlog_lock();     /* now locked */
    if (flags & MLOG_LOGPID) {
        snprintf(newtag, tagblen, "%s[%d]", tag, getpid());
    } else {
        snprintf(newtag, tagblen, "%s", tag);
    }
    mst.def_mask = default_mask;
    mst.stderr_mask = stderr_mask;
    if (logfile) {
        mst.logfile = strdup(logfile);
        if (!mst.logfile) {
            goto error;
        }
        mst.logfd = open(mst.logfile, O_RDWR|O_APPEND|O_CREAT, 0666);
        if (mst.logfd < 0) {
            fprintf(stderr, "mlog_open: cannot open %s: %s\n",
                    mst.logfile, strerror(errno));
            goto error;
        }
    }
    /*
     * save setting of MLOG_SYSLOG and MLOG_UCON_ON bits until these
     * features are actually enabled.  this allows us to use
     * mlog_close() to clean up after us if we encounter an error.
     */
    mst.oflags = (flags & ~(MLOG_SYSLOG|MLOG_UCON_ON));
    /* maxfac_hint should include default fac. */
    if (mlog_setnfac((maxfac_hint < 1) ? 1 : maxfac_hint) < 0) {
        goto error;
    }
    if (msgbuf_len) {
        mst.mb = calloc(1, msgbuf_len + sizeof(struct mlog_mbhead));
        if (!mst.mb) {
            goto error;
        }
        mb = (struct mlog_mbhead *)mst.mb;
        memcpy(mb, MBH_START, sizeof(mb->mbh_start));
        mb->mbh_beef = 0xdeadbeef;
        mb->mbh_len = msgbuf_len;
        mb->mbh_cnt = 0;
        mb->mbh_wp = 0;
    }
    if (flags & MLOG_UCON_ON) {
        mst.udpsock = socket(PF_INET, SOCK_DGRAM, 0);
        if (mst.udpsock < 0) {
            goto error;
        }
        mst.oflags |= MLOG_UCON_ON;
        /* note that mst.{ucon_nslots,ucon_cnt,ucons} are all 0 */
    }
    if ((flags & MLOG_UCON_ENV) != 0 && (dcon = getenv("MLOG_UCON")) != 0) {
        for (mst.ucon_cnt = 1, cp = dcon ; *cp ; cp++) {
            if (*cp == ';') {
                mst.ucon_cnt++;
            }
        }
        mst.ucons = calloc(1, mst.ucon_cnt * sizeof(*mst.ucons));
        if (!mst.ucons) {
            goto error;
        }
        mst.ucon_nslots = mst.ucon_cnt;
        mst.ucon_cnt = mlog_getucon(mst.ucon_cnt, mst.ucons, dcon);
        /*
         * note that it is possible to load ucons but still have the
         * console disabled (e.g. !UCON_ON && UCON_ENV).   in that case
         * the program may enable ucon later via mlog_ucon_on().
         */
    }
    (void) uname(&mst.uts);
    mlog_xst.nodename = mst.uts.nodename; /* expose this */
    /* chop off the domainname */
    if ((flags & MLOG_FQDN) == 0) {
        for (cp = mst.uts.nodename ; *cp && *cp != '.' ; cp++)
            /*null*/;
        *cp = 0;
    }
    /* cache value of isatty() to avoid extra system calls */
    mst.stdout_isatty = isatty(fileno(stdout));
    mst.stderr_isatty = isatty(fileno(stderr));
    /*
     * log now open!
     */
    if (flags & MLOG_SYSLOG) {
        openlog(tag, (flags & MLOG_LOGPID) ? LOG_PID : 0, syslogfac);
        mst.oflags |= MLOG_SYSLOG;
    }
    mlog_xst.tag = newtag;
    mlog_unlock();
    return(0);
error:
    /*
     * we failed.  mlog_cleanout can handle the cleanup for us.
     */
    free(newtag);    /* was never installed */
    mlog_unlock();
    mlog_cleanout();
    return(-1);
}

/*
 * mlog_reopen: reopen a multilog.   reopen logfile for rotation or
 * after a fork...  update ucon and pid in tag (if enabled).
 */
int mlog_reopen(char *logfile)
{
    int rv;
    char *oldpid, *sdup;
    if (!mlog_xst.tag) {
        return(-1);    /* log wasn't open in the first place */
    }
    rv = 0;
    mlog_lock();       /* lock it down */
    /* reset ucon if open */
    if (mst.oflags & MLOG_UCON_ON) {
        if (mst.udpsock >= 0) {
            close(mst.udpsock);
        }
        mst.udpsock = socket(PF_INET, SOCK_DGRAM, 0);
        if (mst.udpsock == -1) {
            mst.oflags &= ~MLOG_UCON_ON;    /* unlikely */
        }
    }
    /*
     * refresh the pid - mlog_open pads the tag such that we cannot
     * overflow by snprinting an int pid here...
     */
    if ((mst.oflags & MLOG_LOGPID) != 0 &&
            (oldpid = strrchr(mlog_xst.tag, '[')) != NULL) {
        snprintf(oldpid, MLOG_TAGPAD, "[%d]", getpid());
    }
    if (mst.logfd >= 0) {
        (void) close(mst.logfd);
    }
    mst.logfd = -1;
    /* now the log file */
    if (logfile == NULL) {    /* don't want a log file */
        if (mst.logfile) {    /* flush out any old stuff */
            free(mst.logfile);
            mst.logfile = NULL;
        }
    } else if (logfile[0] != '\0' &&
               (mst.logfile == NULL || strcmp(mst.logfile, logfile) != 0)) {
        /*
         * we are here if we have a new logfile name requested and it
         * different from what was there before, so we need to malloc a
         * new mst.logfile.
         */
        sdup = strdup(logfile);
        if (sdup == NULL) {
            fprintf(stderr, "mlog_reopen: out of memory - strdup(%s)\n",
                    logfile);
            /* XXX: what else can we do? */
            rv = -1;
            goto done;
        }
        if (mst.logfile) {
            free(mst.logfile);    /* dump the old one, if present */
        }
        mst.logfile = sdup;       /* install the new one */
    }
    if (mst.logfile) {
        mst.logfd = open(mst.logfile, O_RDWR|O_APPEND|O_CREAT, 0666);
        if (mst.logfd < 0) {
            fprintf(stderr, "mlog_reopen: cannot reopen logfile %s: %s\n",
                    mst.logfile, strerror(errno));
            rv = -1;
        }
    }
done:
    mlog_unlock();
    return(rv);
}

/*
 * mlog_close: close off an mlog and release any allocated resources
 * (e.g. as part of an orderly shutdown, after all worker threads have
 * been collected). if already closed, this function is a noop.
 */
void mlog_close()
{
    if (!mlog_xst.tag) {
        return;    /* return if already closed */
    }
    free(mlog_xst.tag);
    mlog_xst.tag = NULL;       /* marks us as down */
    mlog_cleanout();
}

/*
 * mlog_namefacility: assign a name to a facility
 * return 0 on success, -1 on error (malloc problem).
 */
int mlog_namefacility(int facility, char *aname, char *lname)
{
    int rv;
    char *n, *nl;
    /* not open? */
    if (!mlog_xst.tag) {
        return(-1);
    }
    rv = -1;      /* assume error */
    mlog_lock();
    /* need to allocate facility? */
    if (facility >= mlog_xst.fac_cnt) {
        if (mlog_setnfac(facility+1) < 0) {
            goto done;
        }
    }
    n = 0;
    nl = 0;
    if (aname) {
        n = strdup(aname);
        if (!n) {
            goto done;
        }
        if (lname && (nl = strdup(lname)) == NULL) {
            free(n);
            goto done;
        }
    }
    if (mlog_xst.mlog_facs[facility].fac_aname &&
            mlog_xst.mlog_facs[facility].fac_aname != default_fac0name) {
        free(mlog_xst.mlog_facs[facility].fac_aname);
    }
    if (mlog_xst.mlog_facs[facility].fac_lname) {
        free(mlog_xst.mlog_facs[facility].fac_lname);
    }
    mlog_xst.mlog_facs[facility].fac_aname = n;
    mlog_xst.mlog_facs[facility].fac_lname = nl;
    rv = 0;    /* now we have success */
done:
    mlog_unlock();
    return(rv);
}

/*
 * mlog_allocfacility: allocate a new facility with the given name.
 * return new facility number on success, -1 on error (malloc problem).
 */
int mlog_allocfacility(char *aname, char *lname)
{
    int newfac;
    /* not open? */
    if (!mlog_xst.tag) {
        return(-1);
    }
    mlog_lock();
    newfac = mlog_xst.fac_cnt;
    if (mlog_setnfac(newfac+1) < 0) {
        newfac = -1;
    }
    mlog_unlock();
    if (newfac == -1 || mlog_namefacility(newfac, aname, lname) < 0) {
        return(-1);
    }
    return(newfac);
}

/*
 * mlog_setlogmask: set the logmask for a given facility.  if the user
 * uses a new facility, we ensure that our facility array covers it
 * (expanding as needed).  return oldmask on success, -1 on error.  cannot
 * fail if facility array was preallocated.
 */
int mlog_setlogmask(int facility, int mask)
{
    int oldmask;
    /* not open? */
    if (!mlog_xst.tag) {
        return(-1);
    }
    mlog_lock();
    /* need to allocate facility? */
    if (facility >= mlog_xst.fac_cnt && mlog_setnfac(facility+1) < 0) {
        oldmask = -1;   /* error */
    } else {
        /* swap it in, masking out any naughty bits */
        oldmask = mlog_xst.mlog_facs[facility].fac_mask;
        mlog_xst.mlog_facs[facility].fac_mask = (mask & MLOG_PRIMASK);
    }
    mlog_unlock();
    return(oldmask);
}

/*
 * mlog_setmasks: set the mlog masks for a set of facilities to a given
 * level.   the input string should look: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
 * if the "PREFIX=" part is omitted, then the level applies to all defined
 * facilities (e.g. mlog_setmasks("WARN") sets everything to WARN).
 */
void mlog_setmasks(char *mstr, int mlen0)
{
    char *m, *current, *fac, *pri, pbuf[8];
    int mlen, facno, clen, elen, faclen, prilen, prino;
    /* not open? */
    if (!mlog_xst.tag) {
        return;
    }
    m = mstr;
    mlen = mlen0;
    if (mlen < 0)  {
        mlen = strlen(mstr);
    }
    while (mlen > 0 && (*m == ' ' || *m == '\t')) { /* remove leading space */
        m++;
        mlen--;
    }
    if (mlen <= 0) {
        return;                       /* nothing doing */
    }
    facno = 0;                        /* make sure it gets init'd */
    while (m) {
        /* note current chunk, and advance m to the next one */
        current = m;
        for (clen = 0 ; clen < mlen && m[clen] != ',' ; clen++) {
            /*null*/;
        }
        if (clen < mlen) {
            m = m + clen + 1;   /* skip the comma too */
            mlen = mlen - (clen + 1);
        } else {
            m = NULL;
            mlen = 0;
        }
        if (clen == 0) {
            continue;     /* null entry, just skip it */
        }
        for (elen = 0 ; elen < clen && current[elen] != '=' ; elen++) {
            /*null*/;
        }
        if (elen < clen) {     /* has a facility prefix? */
            fac = current;
            faclen = elen;
            pri = current + elen + 1;
            prilen = clen - (elen + 1);
        } else {
            fac = NULL;                /* means we apply to all facs */
            faclen = 0;
            pri = current;
            prilen = clen;
        }
        if (m == NULL) {
            /* remove trailing white space from count */
            while (prilen > 0 && (pri[prilen-1] == '\n' ||
                                  pri[prilen-1] == ' ' ||
                                  pri[prilen-1] == '\t') ) {
                prilen--;
            }
        }
        /* parse complete! */
        /* process priority */
        if (prilen > 5) {    /* we know it can't be longer than this */
            prino = -1;
        } else {
            memset(pbuf, 0, sizeof(pbuf));
            strncpy(pbuf, pri, prilen);
            prino = mlog_str2pri(pbuf);
        }
        if (prino == -1) {
            mlog(MLOG_ERR, "mlog_setmasks: %.*s: unknown priority %.*s",
                 faclen, fac, prilen, pri);
            continue;
        }
        /* process facility */
        if (fac) {
            mlog_lock();
            for (facno = 0 ; facno < mlog_xst.fac_cnt ; facno++) {
                if (mlog_xst.mlog_facs[facno].fac_aname &&
                        strlen(mlog_xst.mlog_facs[facno].fac_aname) == faclen &&
                        strncasecmp(mlog_xst.mlog_facs[facno].fac_aname, fac,
                                    faclen) == 0) {
                    break;
                }
                if (mlog_xst.mlog_facs[facno].fac_lname &&
                        strlen(mlog_xst.mlog_facs[facno].fac_lname) == faclen &&
                        strncasecmp(mlog_xst.mlog_facs[facno].fac_lname, fac,
                                    faclen) == 0) {
                    break;
                }
            }
            mlog_unlock();
            if (facno >= mlog_xst.fac_cnt) {
                mlog(MLOG_ERR, "mlog_setmasks: unknown facility %.*s",
                     faclen, fac);
                continue;
            }
        }
        if (fac) {
            /* apply only to this fac */
            mlog_setlogmask(facno, prino);
        } else {
            /* apply to all facilities */
            for (facno = 0 ; facno < mlog_xst.fac_cnt ; facno++) {
                mlog_setlogmask(facno, prino);
            }
        }
    }
}

/*
 * mlog_getmasks: get current masks levels
 */
int mlog_getmasks(char *buf, int discard, int len, int unterm)
{
    char *bp, *myname;
    const char *p;
    int skipcnt, resid, total, facno;
    char store[64];   /* fac unlikely to overflow this */
    /* not open? */
    if (!mlog_xst.tag) {
        return(0);
    }
    bp = buf;
    skipcnt = discard;
    resid = len;
    total = 0;
    mlog_lock();
    for (facno = 0 ; facno < mlog_xst.fac_cnt ; facno++) {
        if (facno) {
            mlog_bput(&bp, &skipcnt, &resid, &total, ",");
        }
        if (mlog_xst.mlog_facs[facno].fac_lname != NULL) {
            myname = mlog_xst.mlog_facs[facno].fac_lname;
        } else {
            myname = mlog_xst.mlog_facs[facno].fac_aname;
        }
        if (myname == NULL) {
            snprintf(store, sizeof(store), "%d", facno);
            mlog_bput(&bp, &skipcnt, &resid, &total, store);
        } else {
            mlog_bput(&bp, &skipcnt, &resid, &total, myname);
        }
        mlog_bput(&bp, &skipcnt, &resid, &total, "=");
        p = mlog_pristr(mlog_xst.mlog_facs[facno].fac_mask);
        store[1] = 0;
        while (*p && *p != ' ' && *p != '-') {
            store[0] = *p;
            p++;
            mlog_bput(&bp, &skipcnt, &resid, &total, store);
        }
    }
    mlog_unlock();
    strncpy(store, "\n", sizeof(store));
    mlog_bput(&bp, &skipcnt, &resid, &total, store);
    if (unterm == 0) {
        mlog_bput(&bp, &skipcnt, &resid, &total, NULL);
    }
    /* buf == NULL means probe for length ... */
    return((buf == NULL) ? total : len - resid);
}

/*
 * mlog_abort_hook: set mlog abort hook
 */
void *mlog_abort_hook(void (*abort_hook)(void))
{
    void *ret;
    if (mlog_xst.tag) {
        mlog_lock();
        ret = mst.abort_hook;           /* save old value for return */
        mst.abort_hook = abort_hook;
        mlog_unlock();
    } else {
        ret = NULL;
    }
    return(ret);
}

/*
 * mlog_dmesg: obtain pointers to the current contents of the message
 * buffer.   since the message buffer is circular, the result may come
 * back in two pieces.
 * return 0 on success, -1 on error (not open, or no message buffer)
 */
int mlog_dmesg(char **b1p, int *b1len, char **b2p, int *b2len)
{
    /* first check if we are open and have the buffer */
    if (!mlog_xst.tag || !mst.mb) {
        return(-1);
    }
    mlog_lock();
    mlog_dmesg_mbuf(b1p, b1len, b2p, b2len);
    mlog_unlock();
    return(0);
}

/*
 * mlog_mbcount: give a hint as to the current size of the message buffer.
 */
int mlog_mbcount()
{
    struct mlog_mbhead *mb;
    int rv;
    /* first check if we are open and have the buffer */
    if (!mlog_xst.tag || !mst.mb) {
        return(0);
    }
    mlog_lock();
    rv = 0;
    mb = (struct mlog_mbhead *)mst.mb;
    if (mb) {
        rv = mb->mbh_cnt;
    }
    mlog_unlock();
    return(rv);
}

/*
 * mlog_mbcopy: safely copy the most recent bytes of the message buffer
 * over into another buffer for use.   returns # of bytes copied, -1 on
 * error.
 */
int mlog_mbcopy(char *buf, int offset, int len)
{
    char *b1, *b2, *bp;
    int b1l, b2l, got, want, skip;
    if (!buf || len < 1 || !mlog_xst.tag) {
        return(-1);
    }
    if (!mst.mb) {
        return(0);    /* no message buffer, treat like reading /dev/null? */
    }
    mlog_lock();
    mlog_dmesg_mbuf(&b1, &b1l, &b2, &b2l);
    /* pull back from the newest data by 'offset' bytes */
    if (offset > 0 && b2l > 0) {
        if (offset > b2l) {
            offset -= b2l;
            b2l = 0;
        } else {
            b2l -= offset;
            offset = 0;
        }
    }
    if (offset > 0 && b1l > 0) {
        if (offset > b1l) {
            b1l = 0;
        } else {
            b1l -= offset;
        }
    }
    got = b1l + b2l;                      /* total bytes in msg buf */
    want = (len > got) ? got : len;       /* how many we want, capped by got */
    skip = (want < got) ? got - want : 0; /* how many we skip over */
    if (skip) {
        if (skip > b1l) {
            skip -= b1l;
            b1l = 0;
        } else {
            b1l -= skip;
            b1 += skip;
            skip = 0;
        }
        if (skip > b2l) {
            b2l = 0;
        } else {
            b2l -= skip;
            b2 += skip;
        }
    }
    bp = buf;
    if (b1l) {
        memcpy(bp, b1, b1l);
        bp += b1l;
    }
    if (b2l) {
        memcpy(bp, b2, b2l);
    }
    mlog_unlock();
    return(want);
}

/* XXXCDC: BEGIN TMP */
/*
 * plfs_debug: tmp wrapper
 */
void plfs_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmlog(MLOG_DBG, fmt, ap);
    va_end(ap);
}
/* XXXCDC: END TMP */

/*
 * mlog: multilog a message... generic wrapper for the the core vmlog
 * function.  note that a log line cannot be larger than MLOG_TBSZ (4096)
 * [if it is larger it will be (silently) truncated].
 */
void mlog(int flags, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmlog(flags, fmt, ap);
    va_end(ap);
}

/*
 * mlog_abort: like mlog, but prints the stack and does an abort after
 * processing the log. for aborts, we always log to STDERR.
 */
void mlog_abort(int flags, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmlog(flags|MLOG_STDERR, fmt, ap);
    va_end(ap);
    if (mlog_xst.tag && mst.abort_hook) { /* call hook? */
        mst.abort_hook();
    }
    abort();
    /*NOTREACHED*/
}

/*
 * mlog_exit: like mlog, but exits with the given status after processing
 * the log.   we always log to STDERR.
 */
void mlog_exit(int status, int flags, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmlog(flags|MLOG_STDERR, fmt, ap);
    va_end(ap);
    exit(status);
    /*NOTREACHED*/
}

/*
 * mlog_findmesgbuf: search for a message buffer inside another buffer
 * (typically a mmaped core file).  does not access mlog global state.
 * return 0 on success, -1 on error
 */
int mlog_findmesgbuf(char *b, int len, char **b1p, int *b1l,
                     char **b2p, int *b2l)
{
    char *ptr, *headend, *bufend;
    struct mlog_mbhead mb;
    uint32_t skip;
    ptr = b;                                 /* current pointer */
    bufend = b + len;                        /* end of buffer */
    headend = bufend - sizeof(struct mlog_mbhead);  /* can't start from here */
    for (/*null*/ ; ptr < headend ; ptr += 4) {
        if (memcmp(ptr, MBH_START, sizeof(MBH_START) - 1) != 0) {
            continue;
        }
        /*
         * might have found it.  handle byte order and sanity check it.
         */
        memcpy(&mb, ptr, sizeof(mb));    /* make a copy to ensure alignment */
        if (mb.mbh_beef == wswap(0xdeadbeef)) {
            mb.mbh_len = wswap(mb.mbh_len);
            mb.mbh_cnt = wswap(mb.mbh_cnt);
            mb.mbh_wp  = wswap(mb.mbh_wp);
        }
        if (mb.mbh_cnt > mb.mbh_len) {
            continue;
        }
        if (mb.mbh_wp > mb.mbh_len) {
            continue;
        }
        if (ptr + mb.mbh_len > bufend ||
                ptr + mb.mbh_len < ptr) {
            continue;
        }
        /*
         * looks good!
         */
        *b1p = ptr + sizeof(mb) + mb.mbh_wp;
        *b1l = mb.mbh_len - mb.mbh_wp;
        *b2p = ptr + sizeof(mb);
        *b2l = mb.mbh_wp;
        skip = mb.mbh_len - mb.mbh_cnt;
        if (skip > *b1l) {
            skip -= *b1l;
            *b1p = *b2p;
            *b2p = 0;
            *b1l = *b2l;
            *b2l = 0;
        }
        if (skip) {
            *b1p = *b1p + skip;
            *b1l = *b1l - skip;
        }
        return(0);
    }
    return(-1);
}

/*
 * mlog_ucon_on: enable ucon (UDP console)
 * return 0 on success, -1 on error
 */
int mlog_ucon_on()
{
    /* ensure open before doing stuff */
    if (!mlog_xst.tag) {
        return(-1);
    }
    /* note that mst.ucons/mst.ucon_cnt must already be valid */
    mlog_lock();
    if ((mst.oflags & MLOG_UCON_ON) == 0) {
        mst.udpsock = socket(PF_INET, SOCK_DGRAM, 0);
        if (mst.udpsock >= 0) {
            mst.oflags |= MLOG_UCON_ON;
        }
    }
    mlog_unlock();
    return( ((mst.oflags & MLOG_UCON_ON) != 0) ? 0 : -1);
}

/*
 * mlog_ucon_off: disable ucon (UDP console) if enabled
 * return 0 on success, -1 on error
 */
int mlog_ucon_off()
{
    /* ensure open before doing stuff */
    if (!mlog_xst.tag) {
        return(-1);
    }
    mlog_lock();
    mst.oflags = mst.oflags & ~MLOG_UCON_ON;
    if (mst.udpsock >= 0) {
        close(mst.udpsock);
    }
    mst.udpsock = -1;
    mlog_unlock();
    return(0);
}

/*
 * mlog_ucon_add: add an endpoint as a ucon
 * return 0 on success, -1 on error
 */
int mlog_ucon_add(char *host, int port)
{
    char portstr[8];
    int rv, sz;
    void *newbuf;
    /* ensure open and sane before doing stuff */
    if (!mlog_xst.tag || port < 1 || port > 65535) {
        return(-1);
    }
    rv = -1;   /* assume fail */
    mlog_lock();
    /* grow the array if necessary */
    if (mst.ucon_cnt == mst.ucon_nslots) {
        sz = mst.ucon_cnt + 1;
        newbuf = calloc(1, sz * sizeof(*mst.ucons));
        if (!newbuf) {
            goto done;
        }
        if (mst.ucons) {
            memcpy(newbuf, mst.ucons, mst.ucon_cnt * sizeof(*mst.ucons));
            free(mst.ucons);
        }
        mst.ucons = newbuf;
        mst.ucon_nslots = mst.ucon_cnt + 1;
    }
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (mlog_resolvhost(&mst.ucons[mst.ucon_cnt], host, portstr) < 0) {
        goto done;
    }
    /* got it! */
    mst.ucon_cnt++;
    rv = 0;
done:
    mlog_unlock();
    return(rv);
}

/*
 * mlog_ucon_rm: remove an ucon endpoint (port in host byte order).
 * return 0 on success, -1 on error
 */
int mlog_ucon_rm(char *host, int port)
{
    char portstr[8];
    struct sockaddr_in target;
    int rv, lcv;
    /* ensure open and sane before doing stuff */
    if (!mlog_xst.tag || port < 1 || port > 65535  || mst.ucon_cnt < 1) {
        return(-1);
    }
    /* resolve the hostname */
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (mlog_resolvhost(&target, host, portstr) < 0) {
        return(-1);
    }
    rv = -1;
    mlog_lock();
    /* look for it ... */
    for (lcv = 0 ; lcv < mst.ucon_cnt ; lcv++) {
        if (memcmp(&target, &mst.ucons[lcv], sizeof(*mst.ucons)) == 0) {
            break;
        }
    }
    /* didn't find it ? */
    if (lcv >= mst.ucon_cnt) {
        goto done;
    }
    /* if not the last item in the list, pull that item forward */
    if (lcv < mst.ucon_cnt - 1) {
        memcpy(&mst.ucons[lcv], &mst.ucons[mst.ucon_cnt - 1],
               sizeof(*mst.ucons));
    }
    /* remove last item in list */
    mst.ucon_cnt--;
    rv = 0;
    /*
     * done!
     */
done:
    mlog_unlock();
    return(rv);
}

