/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

#include "unifycr-runtime-config.h"
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <libgen.h>
#include <limits.h>
#define __USE_GNU
#include <pthread.h>
#include "unifycr-stdio.h"
#include "unifycr-internal.h"

static int unifycr_fpos_enabled = 1; /* whether we can use fgetpos/fsetpos */

/* ---------------------------------------
 * POSIX wrappers: file streams
 * --------------------------------------- */

/* Known Problems:
 *  - does not support wide character calls
 *  - does not support wide cahracter formatting
 *  - hard coded to use U.S. Locale (decimal point)
 *  - if putc/getc are macros we can't wrap them
 *  - streams are not locked
 *    http://www.gnu.org/software/libc/manual/html_node/Streams-and-Threads.html#Streams-and-Threads
 *  - does not handle %as like GNU C */

/*
 * FreeBSD http://www.freebsd.org/
 *   svn co svn://svn.freebsd.org/base/head/lib/libc/ freebsd_libc.svn
 *`
 * Bionic libc BSD from google
 * https://github.com/android/platform_bionic/blob/master/libc/docs/OVERVIEW.TXT
 *
 * LGPL libc http://uclibc.org/
 *
 * http://www.gnu.org/software/libc/manual/html_node/I_002fO-Overview.html#I_002fO-Overview
 * http://pubs.opengroup.org/onlinepubs/009695399/
 * http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf (7.19)
 * https://github.com/fakechroot/fakechroot/tree/master/src
 */

/* http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf 7.19 */

/* TODO: add "unifycr_unsupported" call to report details of unsupported fns */

/* given a stream, return file name or NULL if invalid */
static const char *unifycr_stream_name(FILE *fp)
{
    /* convert to unifycr_stream_t pointer */
    unifycr_stream_t *s = (unifycr_stream_t *) fp;

    /* get name of file */
    const char *name = NULL;
    int fid = unifycr_get_fid_from_fd(s->fd);
    if (fid >= 0) {
        name = unifycr_filelist[fid].filename;
    }
    return name;
}

int unifycr_unsupported_stream(
    FILE *fp,
    const char *wrap_fn,
    const char *wrap_file,
    int         wrap_line,
    const char *format,
    ...)
{
    /* convert to unifycr_stream_t pointer */
    unifycr_stream_t *s = (unifycr_stream_t *) fp;

    /* get name of file */
    const char *name = unifycr_stream_name(fp);

    /* get file position */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    off_t pos = filedesc->pos;

    /* determine length of string to hold formatted args */
    va_list args1;
    va_start(args1, format);
    int strlen = vsnprintf(NULL, 0, format, args1);
    va_end(args1);

    /* allocate memory for string */
    int chars = strlen + 1;
    char *str = (char *) malloc(chars);
    if (str == NULL) {
        /* Error */
    }

    /* create the string */
    va_list args2;
    va_start(args2, format);
    vsnprintf(str, chars, format, args2);
    va_end(args2);

    /* print message */
    va_list args;
    va_start(args, format);
    int rc = unifycr_unsupported(
                 wrap_fn, wrap_file, wrap_line,
                 "file %s pos %lu msg %s", name, (unsigned long) pos, str
             );
    va_end(args);

    /* free the string */
    free(str);

    return rc;
}

int unifycr_stream_set_pointers(unifycr_stream_t *s)
{
    /* get pointer to file descriptor structure */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    if (filedesc == NULL) {
        /* ERROR: invalid file descriptor */
        s->err = 1;
        errno = EBADF;
        return UNIFYCR_ERROR_BADF;
    }

    /* if we have anything on the push back buffer, that must be
     * our current file pointer, since any seek would have cleared
     * the buffer and each read/write/unget keeps it up-to-date */
    if (s->ubuflen > 0) {
        s->_p = s->ubuf + s->ubufsize - s->ubuflen;
        s->_r = s->ubuflen;
        return UNIFYCR_SUCCESS;
    }

    /* check that current falls within buffer */
    off_t current = filedesc->pos;
    off_t start  = s->bufpos;
    off_t length = s->buflen;
    if (current < start || current >= start + length) {
        /* file position is out of range of current buffer */
        s->_p = NULL;
        s->_r = 0;
    } else {
        /* determine number of bytes to copy from stream buffer */
        size_t stream_offset    = (size_t)(current - s->bufpos);
        char  *stream_start     = (char *)s->buf + stream_offset;
        size_t stream_remaining = s->buflen - stream_offset;
        s->_p = (unsigned char *) stream_start;
        s->_r = stream_remaining;
    }

    return UNIFYCR_SUCCESS;
}

/* TODO: support other modes as listed in
 * http://www.gnu.org/software/libc/manual/html_node/Opening-Streams.html#Opening-Streams */

/* given a mode like "r", "wb+", or "a+" return flags read, write,
 * append, and plus to indicate which were set,
 * returns UNIFYCR_ERROR_INVAL if invalid character is found
 */
static int unifycr_fopen_parse_mode(
    const char *mode,
    int *read,
    int *write,
    int *append,
    int *plus)
{
    /* we'll set each of these to 1 as we find them */
    *read   = 0;
    *write  = 0;
    *append = 0;
    *plus   = 0;

    /* ensure that user specifed an input mode */
    if (mode == NULL) {
        return UNIFYCR_ERROR_INVAL;
    }

    /* get number of characters in mode */
    size_t len = strlen(mode);
    if (len <= 0 || len > 3) {
        return UNIFYCR_ERROR_INVAL;
    }

    /* first character must either be r, w, or a */
    char first = mode[0];
    switch (first) {
    case 'r':
        *read = 1;
        break;
    case 'w':
        *write = 1;
        break;
    case 'a':
        *append = 1;
        break;
    default:
        return UNIFYCR_ERROR_INVAL;
    }

    /* optional second character may either be + or b */
    if (len > 1) {
        char second = mode[1];
        if (second == '+') {
            /* second character is a plus */
            *plus = 1;

            /* if there is a third character, it must be b */
            if (len > 2) {
                char third = mode[2];
                if (third != 'b') {
                    /* third character something other than + or b */
                    return UNIFYCR_ERROR_INVAL;
                }
            }
        } else if (second == 'b') {
            /* second character is a b, if there is a third it must be + */
            if (len > 2) {
                char third = mode[2];
                if (third == '+') {
                    *plus = 1;
                } else {
                    /* third character something other than + or b */
                    return UNIFYCR_ERROR_INVAL;
                }
            }
        } else {
            /* second character something other than + or b */
            return UNIFYCR_ERROR_INVAL;
        }
    }

    return UNIFYCR_SUCCESS;
}

/* calls unifycr_fid_open to open specified file in mode according to
 * fopen mode semantics, initializes outstream and returns
 * UNIFYCR_SUCCESS if successful, returns some other UNIFYCR error
 * otherwise */
static int unifycr_fopen(
    const char *path,
    const char *mode,
    FILE **outstream)
{
    /* assume that we'll fail */
    *outstream = NULL;

    /* parse the fopen mode string */
    int read, write, append, plus;
    int parse_rc = unifycr_fopen_parse_mode(mode, &read, &write, &append, &plus);
    if (parse_rc != UNIFYCR_SUCCESS) {
        return parse_rc;
    }

    /* TODO: get real permissions via umask */
    /* assume default permissions */
    mode_t perms = unifycr_getmode(0);

    int open_rc = -1;
    int fid;
    off_t pos;
    if (read) {
        /* read shall fail if file does not already exist, unifycr_fid_open
         * returns UNIFYCR_ERROR_NOENT if file does not exist w/o O_CREAT
         */
        if (plus) {
            /* r+ ==> open file for update (reading and writing) */
            open_rc = unifycr_fid_open(path, O_RDWR, perms, &fid, &pos);
        } else {
            /* r  ==> open file for reading */
            open_rc = unifycr_fid_open(path, O_RDONLY, perms, &fid, &pos);
        }
    } else if (write) {
        if (plus) {
            /* w+ ==> truncate to zero length or create file for update
             * (read/write)
             */
            open_rc = unifycr_fid_open(path, O_RDWR | O_CREAT | O_TRUNC,
                                       perms, &fid, &pos);
        } else {
            /* w  ==> truncate to zero length or create file for writing */
            open_rc = unifycr_fid_open(path, O_WRONLY | O_CREAT | O_TRUNC,
                                       perms, &fid, &pos);
        }
    } else if (append) {
        /* force all writes to end of file when append is set */
        if (plus) {
            /* a+ ==> append, open or create file for update, at end of file */
            open_rc = unifycr_fid_open(path, O_RDWR | O_CREAT | O_APPEND,
                                       perms, &fid, &pos);
        } else {
            /* a ==> append, open or create file for writing, at end of file */
            open_rc = unifycr_fid_open(path, O_WRONLY | O_CREAT | O_APPEND,
                                       perms, &fid, &pos);
        }
    }

    /* check the open return code */
    if (open_rc != UNIFYCR_SUCCESS) {
        return open_rc;
    }

    /* allocate a stream for this file */
    unifycr_stream_t *s = &(unifycr_streams[fid]);

    /* allocate a file descriptor for this file */
    int fd = fid;

    /* clear error and eof indicators and record file descriptor */
    s->err = 0;
    s->eof = 0;
    s->fd  = fd;

    /* record the access mode for the stream */
    s->append = append;

    /* set orientation to NULL */
    s->orient = UNIFYCR_STREAM_ORIENTATION_NULL;

    /* default to fully buffered, set buffer to NULL to indicate
     * setvbuf has not been called */
    s->buf      = NULL;
    s->buffree  = 0;
    s->buftype  = _IOFBF;
    s->bufsize  = 0;
    s->bufpos   = 0;
    s->buflen   = 0;
    s->bufdirty = 0;

    /* initialize the ungetc buffer */
    s->ubuf     = NULL;
    s->ubufsize = 0;
    s->ubuflen  = 0;

    s->_p = NULL;
    s->_r = 0;

    /* set file pointer and read/write mode in file descriptor */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
    filedesc->pos   = pos;
    filedesc->read  = read  || plus;
    filedesc->write = write || plus;

    /* set return parameter and return */
    *outstream = (FILE *)s;
    return UNIFYCR_SUCCESS;
}

/* associate buffer with stream, allocates a buffer of specified size
 * if buf is NULL, otherwise uses buffer passed by caller, also sets
 * stream to fully/line/unbuffered, returns UNIFYCR error codes */
static int unifycr_setvbuf(
    FILE *stream,
    char *buf,
    int type,
    size_t size)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* TODO: check that stream is valid */

    /* check whether we've already associated a buffer */
    if (s->buf != NULL) {
        /* ERROR: stream already has buffer */
        return UNIFYCR_ERROR_BADF;
    }

    /* check that the type argument is valid */
    if (type != _IOFBF && type != _IOLBF && type != _IONBF) {
        /* ERROR: invalid type argument */
        return UNIFYCR_ERROR_INVAL;
    }

    /* check that size is valid */
    if (size <= 0) {
        /* ERROR: invalid size argument */
        return UNIFYCR_ERROR_INVAL;
    }

    /* associate buffer with stream */
    if (buf == NULL) {
        /* allocate buffer */
        s->buf = malloc(size);
        if (s->buf == NULL) {
            /* ERROR: no memory */
            return UNIFYCR_ERROR_NOMEM;
        }
        /* remember that we need to free the buffer at the end */
        s->buffree = 1;
    } else {
        /* caller provided buffer, remember that we don't need to
         * free it when closing the stream */
        s->buf = buf;
        s->buffree = 0;
    }

    /* set properties of buffer */
    s->buftype  = type;
    s->bufsize  = size;
    s->bufpos   = 0;
    s->buflen   = 0;
    s->bufdirty = 0;

    return UNIFYCR_SUCCESS;
}

/* calls unifycr_fd_write to flush stream if it is dirty,
 * returns UNIFYCR error codes, sets stream error indicator and errno
 * upon error */
static int unifycr_stream_flush(FILE *stream)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* TODO: check that stream is valid */

    /* if buffer is dirty, write data to file */
    if (s->buf != NULL && s->bufdirty) {
        int write_rc = unifycr_fd_write(s->fd, s->bufpos, s->buf, s->buflen);
        if (write_rc != UNIFYCR_SUCCESS) {
            s->err = 1;
            errno = unifycr_err_map_to_errno(write_rc);
            return write_rc;
        }

        /* indicate that buffer is now flushed */
        s->bufdirty = 0;
    }

    return UNIFYCR_SUCCESS;
}

/* reads count bytes from stream into buf, sets stream EOF and error
 * indicators as appropriate, sets errno if error, updates file
 * position, returns number of bytes read in retcount, returns UNIFYCR
 * error codes*/
static int unifycr_stream_read(
    FILE *stream,
    void *buf,
    size_t count,
    size_t *retcount)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* clear pointers, will force a reset when refill is called */
    s->_p = NULL;
    s->_r = 0;

    /* get pointer to file descriptor structure */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    if (filedesc == NULL) {
        /* ERROR: invalid file descriptor */
        s->err = 1;
        errno = EBADF;
        return UNIFYCR_ERROR_BADF;
    }

    /* bail with error if stream not open for reading */
    if (! filedesc->read) {
        s->err = 1;
        errno = EBADF;
        return UNIFYCR_ERROR_BADF;
    }

    /* associate buffer with stream if we need to */
    if (s->buf == NULL) {
        int setvbuf_rc = unifycr_setvbuf(stream, NULL, s->buftype,
                                         UNIFYCR_STREAM_BUFSIZE);
        if (setvbuf_rc != UNIFYCR_SUCCESS) {
            /* ERROR: failed to associate buffer */
            s->err = 1;
            errno = unifycr_err_map_to_errno(setvbuf_rc);
            return setvbuf_rc;
        }
    }

    /* don't attempt read if end-of-file indicator is set */
    if (s->eof) {
        return UNIFYCR_FAILURE;
    }

    /* track our current position in the file and number of bytes
     * left to read */
    off_t current = filedesc->pos;
    size_t remaining = count;

    /* check that current + count doesn't overflow */
    if (unifycr_would_overflow_offt(current, (off_t) count)) {
        s->err = 1;
        errno = EOVERFLOW;
        return UNIFYCR_ERROR_OVERFLOW;
    }

    /* take bytes from push back buffer if they exist */
    size_t ubuflen = s->ubuflen;
    if (ubuflen > 0) {
        /* determine number of bytes to take from push-back buffer */
        size_t ubuf_chars = ubuflen;
        if (remaining < ubuflen) {
            ubuf_chars = remaining;
        }

        /* copy bytes from push back buffer to user buffer */
        unsigned char *ubuf_start = s->ubuf + s->ubufsize - ubuflen;
        memcpy(buf, ubuf_start, ubuf_chars);

        /* drop bytes from push back buffer */
        s->ubuflen -= ubuf_chars;

        /* update our current file position and remaining count */
        current   += ubuf_chars;
        remaining -= ubuf_chars;
    }

    /* TODO: if count is large enough, read directly to user's
     * buffer and be done with it */

    /* read data from file into buffer */
    int eof = 0;
    while (remaining > 0 && !eof) {
        /* check that current falls within buffer */
        off_t start  = s->bufpos;
        off_t length = s->buflen;
        if (current < start || current >= start + length) {
            /* current is outside the range of our buffer */

            /* flush buffer if needed before read */
            int flush_rc = unifycr_stream_flush(stream);
            if (flush_rc != UNIFYCR_SUCCESS) {
                /* ERROR: flush sets error indicator and errno */
                return flush_rc;
            }

            /* read data from file into buffer */
            size_t bufcount;
            int read_rc = unifycr_fd_read(s->fd, current, s->buf, s->bufsize, &bufcount);
            if (read_rc != UNIFYCR_SUCCESS) {
                /* ERROR: read error, set error indicator and errno */
                s->err = 1;
                errno = unifycr_err_map_to_errno(read_rc);
                return read_rc;
            }

            /* record new buffer range within file */
            s->bufpos  = current;
            s->buflen  = bufcount;

            /* set end-of-file flag if our read was short */
            if (bufcount < s->bufsize) {
                eof = 1;
            }
        }

        /* determine number of bytes to copy from stream buffer */
        size_t stream_offset    = (size_t)(current - s->bufpos);
        size_t stream_remaining = s->buflen - stream_offset;
        size_t bytes = stream_remaining;
        if (bytes > remaining) {
            bytes = remaining;
        }

        /* copy data from stream buffer to user buffer */
        if (bytes > 0) {
            char *buf_start    = (char *)buf + (count - remaining);
            char *stream_start = (char *)s->buf + stream_offset;
            memcpy(buf_start, stream_start, bytes);
        }

        /* update our current position and the number of bytes
         * left to read */
        current   += bytes;
        remaining -= bytes;
    }

    /* set number of bytes read */
    *retcount = (count - remaining);

    /* update file position */
    filedesc->pos += (off_t) * retcount;

    /* set end of file indicator if we hit the end */
    if (*retcount < count) {
        s->eof = 1;
    }

    /* return success */
    return UNIFYCR_SUCCESS;
}

/* writes count bytes from buf to stream, sets stream EOF and error
 * indicators as appropriate, sets errno if error, updates file
 * position, return UNIFYCR error codes */
static int unifycr_stream_write(
    FILE *stream,
    const void *buf,
    size_t count)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* clear pointers, will force a reset when refill is called */
    s->_p = NULL;
    s->_r = 0;

    /* TODO: check that stream is valid */

    /* get pointer to file descriptor structure */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    if (filedesc == NULL) {
        /* ERROR: invalid file descriptor */
        s->err = 1;
        errno = EBADF;
        return UNIFYCR_ERROR_BADF;
    }

    /* bail with error if stream not open for writing */
    if (! filedesc->write) {
        s->err = 1;
        errno = EBADF;
        return UNIFYCR_ERROR_BADF;
    }

    /* TODO: Don't know what to do with push back bytes if write
     * overlaps.  Can't find defined behavior in C and POSIX standards. */

    /* set the position to write */
    off_t current;
    if (s->append) {
        /* if in append mode, always write to end of file */
        int fid = unifycr_get_fid_from_fd(s->fd);
        if (fid < 0) {
            s->err = 1;
            errno = EBADF;
            return UNIFYCR_ERROR_BADF;
        }
        current = unifycr_fid_size(fid);

        /* like a seek, we discard push back bytes */
        s->ubuflen;
    } else {
        /* otherwise, write at current file pointer */
        current = filedesc->pos;

        /* drop bytes from push back if write overlaps */
        if (s->ubuflen > 0) {
            if (count >= s->ubuflen) {
                s->ubuflen = 0;
            } else {
                s->ubuflen -= count;
            }
        }
    }

    /* check that current + count doesn't overflow */
    if (unifycr_would_overflow_offt(current, (off_t) count)) {
        s->err = 1;
        errno = EFBIG;
        return UNIFYCR_ERROR_FBIG;
    }

    /* associate buffer with stream if we need to */
    if (s->buf == NULL) {
        int setvbuf_rc = unifycr_setvbuf(stream, NULL, s->buftype,
                                         UNIFYCR_STREAM_BUFSIZE);
        if (setvbuf_rc != UNIFYCR_SUCCESS) {
            /* ERROR: failed to associate buffer */
            s->err = 1;
            errno = unifycr_err_map_to_errno(setvbuf_rc);
            return setvbuf_rc;
        }
    }

    /* if unbuffered, write data directly to file */
    if (s->buftype == _IONBF) {
        /* write data directly to file */
        int write_rc = unifycr_fd_write(s->fd, current, buf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* ERROR: write error, set error indicator and errno */
            s->err = 1;
            errno = unifycr_err_map_to_errno(write_rc);
            return write_rc;
        }

        /* update file position */
        filedesc->pos = current + (off_t) count;

        return UNIFYCR_SUCCESS;
    }

    /* TODO: if count is large enough, write directly to file
     * and be done with it */

    /* write data from buffer to file */
    size_t remaining = count;
    while (remaining > 0) {
        /* if buffer is clean, set start of buffer to current */
        if (! s->bufdirty) {
            s->bufpos = current;
            s->buflen = 0;
        }

        /* determine number of bytes to copy to buffer and whether
         * we need to flush the stream after the write */
        size_t bytes;
        int need_flush = 0;
        size_t stream_offset    = s->buflen;
        size_t stream_remaining = s->bufsize - stream_offset;
        if (s->buftype == _IOLBF) {
            /* line buffered, scan to first newline or end of
             * user buffer and counts bytes as we go */
            bytes = 0;
            const char *ptr = (const char *)buf + (count - remaining);
            while (bytes < remaining) {
                bytes++;
                if (*ptr == '\n') {
                    /* found a newline, write up to and including newline
                     * then flush stream */
                    need_flush = 1;
                    break;
                }
                ptr++;
            }

            /* error if we exhaust buffer before finding a newline */
            if (bytes > stream_remaining ||
                (bytes == stream_remaining && !need_flush)) {
                /* ERROR: write error, set error indicator and errno */
                s->err = 1;
                errno = ENOMEM;
                return UNIFYCR_ERROR_NOMEM;
            }
        } else {
            /* fully buffered, write until we hit the buffer limit */
            bytes = remaining;
            if (bytes >= stream_remaining) {
                bytes = stream_remaining;
                need_flush = 1;
            }
        }

        /* copy data from user buffer to stream buffer */
        if (bytes > 0) {
            char *buf_start    = (char *)buf + (count - remaining);
            char *stream_start = (char *)s->buf + stream_offset;
            memcpy(stream_start, buf_start, bytes);

            /* mark buffer as dirty and increase number of bytes */
            s->bufdirty = 1;
            s->buflen += bytes;
        }

        /* if we've filled the buffer, flush it */
        if (need_flush) {
            /* flush stream */
            int flush_rc = unifycr_stream_flush(stream);
            if (flush_rc != UNIFYCR_SUCCESS) {
                /* ERROR: flush sets error indicator and errno */
                return flush_rc;
            }
        }

        /* update our current position and the number of bytes
         * left to write */
        current   += bytes;
        remaining -= bytes;
    }

    /* TODO: Don't know whether to update file position for append
     * write or leave position where it is.  glibc seems to update
     * so let's do the same here. */

    /* update file position */
    filedesc->pos = current;

    return UNIFYCR_SUCCESS;
}

/* fseek, fseeko, rewind, and fsetpos all call this function, sets error
 * indicator and errno if necessary, returns -1 on error, returns
 * 0 for success */
static int unifycr_fseek(FILE *stream, off_t offset, int whence)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* clear pointers, will force a reset when refill is called */
    s->_p = NULL;
    s->_r = 0;

    /* get pointer to file descriptor structure */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    if (filedesc == NULL) {
        /* ERROR: invalid file descriptor */
        s->err = 1;
        errno = EBADF;
        return -1;
    }

    /* flush stream if we need to */
    int flush_rc = unifycr_stream_flush(stream);
    if (flush_rc != UNIFYCR_SUCCESS) {
        /* ERROR: flush sets error indicator and errno */
        return -1;
    }

    /* get the file id for this file descriptor */
    int fid = unifycr_get_fid_from_fd(s->fd);
    if (fid < 0) {
        /* couldn't find file id, so assume we're at the end,
         * feof defines to errors */
        s->err = 1;
        errno = EBADF;
        return -1;
    }

    /* get current position */
    off_t current_pos = filedesc->pos;

    /* update current position based on whence and offset */
    off_t filesize;
    switch (whence) {
    case SEEK_SET:
        /* seek to offset */
        current_pos = offset;
        break;
    case SEEK_CUR:
        /* seek to current position + offset */
        if (unifycr_would_overflow_offt(current_pos, offset)) {
            s->err = 1;
            errno  = EOVERFLOW;
            return -1;
        }
        current_pos += offset;
        break;
    case SEEK_END:
        /* seek to EOF + offset */
        filesize = unifycr_fid_size(fid);
        if (unifycr_would_overflow_offt(filesize, offset)) {
            s->err = 1;
            errno  = EOVERFLOW;
            return -1;
        }
        current_pos = filesize + offset;
        break;
    default:
        s->err = 1;
        errno = EINVAL;
        return -1;
    }

    /* discard contents of push back buffer */
    if (s->ubuf != NULL) {
        s->ubuflen = 0;
    }

    /* TODO: only update file descriptor if most recent call is
     * fflush? */
    /* save new position */
    filedesc->pos = current_pos;

    /* clear end-of-file indicator */
    s->eof = 0;

    return 0;
}

FILE *UNIFYCR_WRAP(fopen)(const char *path, const char *mode)
{
    /* check whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        FILE *stream;
        int rc = unifycr_fopen(path, mode, &stream);
        if (rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(rc);
            return NULL;
        }
        return stream;
    } else {
        MAP_OR_FAIL(fopen);
        FILE *ret = UNIFYCR_REAL(fopen)(path, mode);
        return ret;
    }
}

FILE *UNIFYCR_WRAP(freopen)(const char *path, const char *mode, FILE *stream)
{
    /* check whether we should intercept this path */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "new file %s",
                                   path);

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return NULL;
    } else {
        MAP_OR_FAIL(freopen);
        FILE *ret = UNIFYCR_REAL(freopen)(path, mode, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(setvbuf)(FILE *stream, char *buf, int type, size_t size)
{
    /* check whether we should intercept this path */
    if (unifycr_intercept_stream(stream)) {
        int rc = unifycr_setvbuf(stream, buf, type, size);
        if (rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(rc);
            return 1;
        }
        return 0;
    } else {
        MAP_OR_FAIL(setvbuf);
        int ret = UNIFYCR_REAL(setvbuf)(stream, buf, type, size);
        return ret;
    }
}

void UNIFYCR_WRAP(setbuf)(FILE *stream, char *buf)
{
    /* check whether we should intercept this path */
    if (unifycr_intercept_stream(stream)) {
        if (buf != NULL) {
            unifycr_setvbuf(stream, buf, _IOFBF, BUFSIZ);
        } else {
            unifycr_setvbuf(stream, buf, _IONBF, BUFSIZ);
        }
        return;
    } else {
        MAP_OR_FAIL(setbuf);
        UNIFYCR_REAL(setbuf)(stream, buf);
        return;
    }
}

int UNIFYCR_WRAP(ungetc)(int c, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* operation shall fail and input stream left unchanged */
        if (c == EOF) {
            return EOF;
        }

        /* convert int to unsigned char */
        unsigned char uc = (unsigned char) c;

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* get filedescriptor and check that stream is valid */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
        if (filedesc == NULL) {
            return EOF;
        }

        /* check that pos > 0 */
        if (filedesc->pos <= 0) {
            return EOF;
        }

        /* allocate bigger push-back buffer if needed */
        size_t oldsize = s->ubufsize;
        size_t len     = s->ubuflen;
        size_t remaining = oldsize - len;
        if (remaining == 0) {
            /* start with a 32-byte push-back buffer,
             * but double current size if we already have one */
            size_t newsize = 1;
            if (oldsize > 0) {
                newsize = oldsize * 2;
            }

            /* make sure we don't get to big */
            if (newsize > 1024) {
                return EOF;
            }

            /* allocate new buffer */
            unsigned char *newbuf = (unsigned char *) malloc(newsize);
            if (newbuf == NULL) {
                return EOF;
            }

            /* copy old bytes to new buffer and free old buffer */
            if (len > 0) {
                unsigned char *oldbuf = s->ubuf;
                unsigned char *oldstart = oldbuf + oldsize - len;
                unsigned char *newstart = newbuf + newsize - len;
                memcpy(newstart, oldstart, len);
                free(s->ubuf);
            }

            /* record details of new buffer */
            s->ubuf     = newbuf;
            s->ubufsize = newsize;
            s->ubuflen  = len;
        }

        /* push char onto buffer */
        s->ubuflen++;
        unsigned char *pos = s->ubuf + s->ubufsize - s->ubuflen;
        *pos = uc;

        /* decrement file position */
        filedesc->pos--;

        /* update buffer pointer and remaining count */
        s->_p = pos;
        s->_r = s->ubuflen;

        /* clear end-of-file flag */
        s->eof = 0;

        return (int) uc;
    } else {
        MAP_OR_FAIL(ungetc);
        int ret = UNIFYCR_REAL(ungetc)(c, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fgetc)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* read next character from file */
        unsigned char charbuf;
        size_t count = 1;
        size_t retcount;
        int read_rc = unifycr_stream_read(stream, &charbuf, count, &retcount);
        if (read_rc != UNIFYCR_SUCCESS || retcount == 0) {
            /* stream read sets error indicator, EOF indicator,
             * and errno for us */
            return EOF;
        }

        /* return byte read cast as an int */
        return (int) charbuf;
    } else {
        MAP_OR_FAIL(fgetc);
        int ret = UNIFYCR_REAL(fgetc)(stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fputc)(int c, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* write data to file */
        unsigned char charbuf = (unsigned char) c;
        size_t count = 1;
        int write_rc = unifycr_stream_write(stream, &charbuf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* stream write sets error indicator, EOF indicator,
             * and errno for us */
            return EOF;
        }

        /* return value written */
        return (int) charbuf;
    } else {
        MAP_OR_FAIL(fputc);
        int ret = UNIFYCR_REAL(fputc)(c, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(getc)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* read next character from file */
        unsigned char charbuf;
        size_t count = 1;
        size_t retcount;
        int read_rc = unifycr_stream_read(stream, &charbuf, count, &retcount);
        if (read_rc != UNIFYCR_SUCCESS || retcount == 0) {
            /* stream read sets error indicator, EOF indicator,
             * and errno for us */
            return EOF;
        }

        /* return byte read cast as an int */
        return (int) charbuf;
    } else {
        MAP_OR_FAIL(getc);
        int ret = UNIFYCR_REAL(getc)(stream);
        return ret;
    }
}

int UNIFYCR_WRAP(putc)(int c, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* write data to file */
        unsigned char charbuf = (unsigned char) c;
        size_t count = 1;
        int write_rc = unifycr_stream_write(stream, &charbuf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* stream write sets error indicator, EOF indicator,
             * and errno for us */
            return EOF;
        }

        /* return value written */
        return (int) charbuf;
    } else {
        MAP_OR_FAIL(putc);
        int ret = UNIFYCR_REAL(putc)(c, stream);
        return ret;
    }
}

char *UNIFYCR_WRAP(fgets)(char *s, int n, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* TODO: this isn't the most efficient algorithm, but it works,
         * would be faster to read a large block of characters then
         * scan for newline */

        /* lookup stream */
        unifycr_stream_t *stm = (unifycr_stream_t *) stream;

        /* TODO: check that stream is valid */

        /* check that we got a positive buffer size */
        if (n <= 0) {
            /* caller specified a buffer length <= 0 */
            stm->err = 1;
            errno = ENOMEM;
            return NULL;
        }

        /* read one character at a time until we hit a newline, read
         * n-1 characters or hit end of the file (or a read error) */
        int limit = 0;
        while (limit < n - 1) {
            /* read the next character from the file */
            char charbuf;
            size_t retcount;
            int read_rc = unifycr_stream_read(stream, &charbuf, 1, &retcount);
            if (read_rc != UNIFYCR_SUCCESS) {
                /* if error flag is not set, we must have hit EOF,
                 * terminate string before returning */
                if (! stm->err) {
                    s[limit] = '\0';
                }

                /* stream read sets error indicator, EOF indicator,
                 * and errno for us */
                return NULL;
            }

            /* copy character to buffer */
            s[limit] = charbuf;
            limit++;

            /* if we hit a newline, break after copying it */
            if (charbuf == '\n') {
                break;
            }
        }

        /* terminate string with a NUL */
        s[limit] = '\0';

        return s;
    } else {
        MAP_OR_FAIL(fgets);
        char *ret = UNIFYCR_REAL(fgets)(s, n, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fputs)(const char *s, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* TODO: check that s is not NULL */

        /* get length of string, less the NUL-terminating byte */
        size_t count = strlen(s);

        /* write data to file */
        int write_rc = unifycr_stream_write(stream, (const void *)s, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* stream write sets error indicator, EOF indicator,
             * and errno for us */
            return EOF;
        }

        /* return success */
        return 0;
    } else {
        MAP_OR_FAIL(fputs);
        int ret = UNIFYCR_REAL(fputs)(s, stream);
        return ret;
    }
}

size_t UNIFYCR_WRAP(fread)(void *ptr, size_t size, size_t nitems, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* must return 0 and do nothing if size or nitems is zero */
        if (size == 0 || nitems == 0) {
            return 0;
        }

        /* TODO: need to call fgetc size times? */
        /* compute total number of bytes that we'll write */
        size_t count = size * nitems;

        /* read next character from file */
        size_t retcount;
        int read_rc = unifycr_stream_read(stream, ptr, count, &retcount);
        if (read_rc != UNIFYCR_SUCCESS) {
            /* stream read sets error indicator, EOF indicator,
             * and errno for us */
            return 0;
        }

        /* return number of items read */
        if (retcount < count) {
            /* adjust return value if read less data than requested */
            size_t nitems_read = retcount / size;
            return nitems_read;
        } else {
            return nitems;
        }
    } else {
        MAP_OR_FAIL(fread);
        size_t ret = UNIFYCR_REAL(fread)(ptr, size, nitems, stream);
        return ret;
    }
}

size_t UNIFYCR_WRAP(fwrite)(const void *ptr, size_t size, size_t nitems,
                            FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* must return 0 and do nothing if size or nitems is zero */
        if (size == 0 || nitems == 0) {
            return 0;
        }

        /* TODO: need to call fputc size times? */
        /* compute total number of bytes that we'll write */
        size_t count = size * nitems;

        /* write data to file */
        int write_rc = unifycr_stream_write(stream, ptr, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* stream write sets error indicator, EOF indicator,
             * and errno for us */
            return 0;
        }

        /* return number of items written */
        return nitems;
    } else {
        MAP_OR_FAIL(fwrite);
        size_t ret = UNIFYCR_REAL(fwrite)(ptr, size, nitems, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(vfprintf)(FILE *stream, const char *format, va_list ap)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: check that stream is active */


        /* get length of component string */
        va_list ap2;
        va_copy(ap2, ap);
        size_t chars = (size_t) vsnprintf(NULL, 0, format, ap2);
        va_end(ap2);

        /* allocate space to hold string, add one for the terminating NUL */
        size_t strlen = chars + 1;
        char *str = (char *) malloc(strlen);
        if (str == NULL) {
            s->err = 1;
            errno = ENOMEM;
            return -1;
        }

        /* copy formatted string into new memory */
        va_list ap3;
        va_copy(ap3, ap);
        int printf_rc = vsnprintf(str, strlen, format, ap3);
        va_end(ap3);
        if (printf_rc != chars) {
            s->err = 1;
            /* assuming that vsnprintf sets errno for us */
            return printf_rc;
        }

        /* write data to file */
        int write_rc = unifycr_stream_write(stream, str, chars);
        if (write_rc != UNIFYCR_SUCCESS) {
            /* stream write sets error indicator, EOF indicator,
             * and errno for us */
            return -1;
        }

        /* free the string */
        free(str);

        /* return number of bytes written */
        return chars;
    } else {
        va_list ap2;
        va_copy(ap2, ap);
        MAP_OR_FAIL(vfprintf);
        int ret = UNIFYCR_REAL(vfprintf)(stream, format, ap2);
        va_end(ap2);
        return ret;
    }
}

int UNIFYCR_WRAP(fprintf)(FILE *stream, const char *format, ...)
{
    va_list args;
    int ret;

    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* delegate work to vfprintf */
        va_start(args, format);
        ret = UNIFYCR_WRAP(vfprintf)(stream, format, args);
        va_end(args);
        return ret;
    } else {
        va_start(args, format);
        MAP_OR_FAIL(vfprintf);
        ret = UNIFYCR_REAL(vfprintf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

/* need to declare this before calling it */
static int __svfscanf(unifycr_stream_t *fp, const char *fmt0, va_list ap);

int UNIFYCR_WRAP(vfscanf)(FILE *stream, const char *format, va_list ap)
{
    va_list args;
    int ret;

    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        va_copy(args, ap);
        ret = __svfscanf((unifycr_stream_t *)stream, format, args);
        va_end(args);
        return ret;
    } else {
        va_copy(args, ap);
        MAP_OR_FAIL(vfscanf);
        ret = UNIFYCR_REAL(vfscanf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

int UNIFYCR_WRAP(fscanf)(FILE *stream, const char *format, ...)
{
    va_list args;
    int ret;

    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* delegate work to vfscanf */
        va_start(args, format);
        ret = UNIFYCR_WRAP(vfscanf)(stream, format, args);
        va_end(args);
        return ret;
    } else {
        va_start(args, format);
        MAP_OR_FAIL(vfscanf);
        ret = UNIFYCR_REAL(vfscanf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

/* TODO: return error if new position overflows long */
int UNIFYCR_WRAP(fseek)(FILE *stream, long offset, int whence)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        off_t offset_offt = (off_t) offset;
        int rc = unifycr_fseek(stream, offset_offt, whence);
        return rc;
    } else {
        MAP_OR_FAIL(fseek);
        int ret = UNIFYCR_REAL(fseek)(stream, offset, whence);
        return ret;
    }
}

int UNIFYCR_WRAP(fseeko)(FILE *stream, off_t offset, int whence)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        int rc = unifycr_fseek(stream, offset, whence);
        return rc;
    } else {
        MAP_OR_FAIL(fseeko);
        int ret = UNIFYCR_REAL(fseeko)(stream, offset, whence);
        return ret;
    }
}

/* TODO: set EOVERFLOW if position overflows long */
long UNIFYCR_WRAP(ftell)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* get pointer to file descriptor structure */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            s->err = 1;
            errno = EBADF;
            return (long) (-1);
        }

        /* get current position */
        off_t current_pos = filedesc->pos;
        return (long)current_pos;
    } else {
        MAP_OR_FAIL(ftell);
        long ret = UNIFYCR_REAL(ftell)(stream);
        return ret;
    }
}

off_t UNIFYCR_WRAP(ftello)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* get pointer to file descriptor structure */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            s->err = 1;
            errno = EBADF;
            return (off_t) (-1);
        }

        /* get current position */
        off_t current_pos = filedesc->pos;
        return current_pos;
    } else {
        MAP_OR_FAIL(ftello);
        off_t ret = UNIFYCR_REAL(ftello)(stream);
        return ret;
    }
}

/* equivalent to fseek(stream, 0L, SEEK_SET) except shall also clear
 * error indicator */
void UNIFYCR_WRAP(rewind)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: check that stream is active */

        /* seek to front of file */
        int rc = unifycr_fseek(stream, (off_t) 0L, SEEK_SET);

        /* set errno */
        errno = unifycr_err_map_to_errno(rc);

        /* clear error indicator if seek successful */
        if (rc == 0) {
            s->err = 0;
        }

        return;
    } else {
        MAP_OR_FAIL(rewind);
        UNIFYCR_REAL(rewind)(stream);
        return;
    }
}

struct unifycr_fpos_t {
    off_t pos;
};

int UNIFYCR_WRAP(fgetpos)(FILE *stream, fpos_t *pos)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* check that we can store a pointer in fpos_t */
        if (! unifycr_fpos_enabled) {
            errno = EOVERFLOW;
            return 1;
        }

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: check that stream is active */

        /* get file descriptor for stream */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return 1;
        }

        /* allocate memory to hold state of stream */
        struct unifycr_fpos_t *state = malloc(sizeof(struct unifycr_fpos_t));
        if (state == NULL) {
            errno = ENOMEM;
            return 1;
        }

        /* record state */
        state->pos = filedesc->pos;

        /* save pointer to state in output parameter */
        void **ptr = (void **) pos;
        *ptr = (void *) state;

        return 0;
    } else {
        MAP_OR_FAIL(fgetpos);
        int ret = UNIFYCR_REAL(fgetpos)(stream, pos);
        return ret;
    }
}

int UNIFYCR_WRAP(fsetpos)(FILE *stream, const fpos_t *pos)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: check that stream is active */

        /* check that we can store a pointer in fpos_t */
        if (! unifycr_fpos_enabled) {
            s->err = 1;
            errno = EOVERFLOW;
            return -1;
        }

        /* get pointer to state from pos input parameter,
         * assumes pos is a pointer to void*, deference to get value
         * of void*, which we then cast to a state pointer, ugh */
        struct unifycr_fpos_t *state = (struct unifycr_fpos_t *) * (void **) pos;

        /* semantics of fsetpos seem to match a seek */
        int seek_rc = unifycr_fseek(stream, state->pos, SEEK_SET);
        if (seek_rc != 0) {
            return seek_rc;
        }

        /* free memory */
        free(state);

        return 0;
    } else {
        MAP_OR_FAIL(fsetpos);
        int ret = UNIFYCR_REAL(fsetpos)(stream, pos);
        return ret;
    }
}

int UNIFYCR_WRAP(fflush)(FILE *stream)
{
    /* if stream is NULL, flush output on all streams */
    if (stream == NULL) {
        /* first, have real library flush all of its streams,
         * important to do this in this order since it may set errno,
         * which could override our setting for unifycr streams */
        MAP_OR_FAIL(fflush);
        int ret = UNIFYCR_REAL(fflush)(NULL);

        /* flush each active unifycr stream */
        int fid;
        for (fid = 0; fid < UNIFYCR_MAX_FILEDESCS; fid++) {
            /* get stream and check whether it's active */
            unifycr_stream_t *s = &(unifycr_streams[fid]);
            if (s->fd >= 0) {
                /* attempt to flush stream */
                int flush_rc = unifycr_stream_flush((FILE *)s);
                if (flush_rc != UNIFYCR_SUCCESS) {
                    /* ERROR: flush sets error indicator and errno */
                    ret = EOF;
                }
            }
        }

        return ret;
    }

    /* otherwise, check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* TODO: check that stream is active */
        /* flush output on stream */
        int rc = unifycr_stream_flush(stream);

        if (rc != UNIFYCR_SUCCESS) {
            /* ERROR: flush sets error indicator and errno */
            return EOF;
        }

        return 0;
    } else {
        MAP_OR_FAIL(fflush);
        int ret = UNIFYCR_REAL(fflush)(stream);
        return ret;
    }
}

/* return non-zero if and only if end-of-file indicator is set
 * for stream */
int UNIFYCR_WRAP(feof)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: ensure stream is active */

        int ret = s->eof;
        return ret;
    } else {
        MAP_OR_FAIL(feof);
        int ret = UNIFYCR_REAL(feof)(stream);
        return ret;
    }
}

int UNIFYCR_WRAP(ferror)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream and file descriptor */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: ensure stream is active */

        int ret = s->err;
        return ret;
    } else {
        MAP_OR_FAIL(ferror);
        int ret = UNIFYCR_REAL(ferror)(stream);
        return ret;
    }
}

void UNIFYCR_WRAP(clearerr)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: ensure stream is active */

        /* clear error and end-of-file indicators for stream */
        s->err = 0;
        s->eof = 0;
        return;
    } else {
        MAP_OR_FAIL(clearerr);
        UNIFYCR_REAL(clearerr)(stream);
        return;
    }

}

int UNIFYCR_WRAP(fileno)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* TODO: check that stream is valid */
        int fd = s->fd;
        if (fd < 0) {
            errno = EBADF;
            return -1;
        }

        /* return file descriptor associated with stream */
        return fd;
    } else {
        MAP_OR_FAIL(fileno);
        int ret = UNIFYCR_REAL(fileno)(stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fclose)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;

        /* get the file id for this file descriptor */
        int fid = unifycr_get_fid_from_fd(s->fd);
        if (fid < 0) {
            errno = EBADF;
            return EOF;
        }

        /* flush stream */
        int flush_rc = unifycr_stream_flush(stream);
        if (flush_rc != UNIFYCR_SUCCESS) {
            /* ERROR: flush sets error indicator and errno */
            return EOF;
        }

        /* free the buffer */
        if (s->buffree) {
            free(s->buf);
            s->buf = NULL;
            s->buffree = 0;
        }

        /* free the push back buffer */
        if (s->ubuf != NULL) {
            free(s->ubuf);
            s->ubuf = NULL;
        }

        /* close the file */
        int close_rc = unifycr_fid_close(fid);
        if (close_rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(close_rc);
            return EOF;
        }

        /* set file descriptor to -1 to indicate stream is invalid */
        s->fd = -1;

        /* currently a no-op */
        return 0;
    } else {
        MAP_OR_FAIL(fclose);
        int ret = UNIFYCR_REAL(fclose)(stream);
        return ret;
    }
}



int UNIFYCR_WRAP(fwprintf)(FILE *stream, const wchar_t *format, ...)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "%s", format);

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return -1;
    } else {
        va_list args;
        va_start(args, format);
        MAP_OR_FAIL(vfwprintf);
        int ret = UNIFYCR_REAL(vfwprintf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

int UNIFYCR_WRAP(fwscanf)(FILE *stream, const wchar_t *format, ...)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "%s", format);

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return EOF;
    } else {
        va_list args;
        va_start(args, format);
        MAP_OR_FAIL(vfwscanf);
        int ret = UNIFYCR_REAL(vfwscanf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

int UNIFYCR_WRAP(vfwprintf)(FILE *stream, const wchar_t *format, va_list arg)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "%s", format);

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return -1;
    } else {
        va_list args;
        va_copy(args, arg);
        MAP_OR_FAIL(vfwprintf);
        int ret = UNIFYCR_REAL(vfwprintf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

int UNIFYCR_WRAP(vfwscanf)(FILE *stream, const wchar_t *format, va_list arg)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "%s", format);

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return EOF;
    } else {
        va_list args;
        va_copy(args, arg);
        MAP_OR_FAIL(vfwscanf);
        int ret = UNIFYCR_REAL(vfwscanf)(stream, format, args);
        va_end(args);
        return ret;
    }
}

wint_t UNIFYCR_WRAP(fgetwc)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return WEOF;
    } else {
        MAP_OR_FAIL(fgetwc);
        wint_t ret = UNIFYCR_REAL(fgetwc)(stream);
        return ret;
    }
}

wchar_t *UNIFYCR_WRAP(fgetws)(wchar_t *s, int n, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return NULL;
    } else {
        MAP_OR_FAIL(fgetws);
        wchar_t *ret = UNIFYCR_REAL(fgetws)(s, n, stream);
        return ret;
    }
}

wint_t UNIFYCR_WRAP(fputwc)(wchar_t wc, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return WEOF;
    } else {
        MAP_OR_FAIL(fputwc);
        wint_t ret = UNIFYCR_REAL(fputwc)(wc, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fputws)(const wchar_t *s, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return -1;
    } else {
        MAP_OR_FAIL(fputws);
        int ret = UNIFYCR_REAL(fputws)(s, stream);
        return ret;
    }
}

int UNIFYCR_WRAP(fwide)(FILE *stream, int mode)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return 0;
    } else {
        MAP_OR_FAIL(fwide);
        int ret = UNIFYCR_REAL(fwide)(stream, mode);
        return ret;
    }
}

wint_t UNIFYCR_WRAP(getwc)(FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return WEOF;
    } else {
        MAP_OR_FAIL(getwc);
        wint_t ret = UNIFYCR_REAL(getwc)(stream);
        return ret;
    }
}

wint_t UNIFYCR_WRAP(putwc)(wchar_t c, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return WEOF;
    } else {
        MAP_OR_FAIL(putwc);
        wint_t ret = UNIFYCR_REAL(putwc)(c, stream);
        return ret;
    }
}

wint_t UNIFYCR_WRAP(ungetwc)(wint_t c, FILE *stream)
{
    /* check whether we should intercept this stream */
    if (unifycr_intercept_stream(stream)) {
        /* return file descriptor associated with stream */
        unifycr_unsupported_stream(stream, __func__, __FILE__, __LINE__, "");

        /* lookup stream */
        unifycr_stream_t *s = (unifycr_stream_t *) stream;
        s->err = 1;
        errno = EIO;
        return WEOF;
    } else {
        MAP_OR_FAIL(ungetwc);
        wint_t ret = UNIFYCR_REAL(ungetwc)(c, stream);
        return ret;
    }
}



/*-
 * Copyright (c) 1990, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "unifycr-stdio.h"
#include "unifycr-internal.h"

#define BUF     513 /* Maximum length of numeric string. */

/*
 * Flags used during conversion.
 */
#define LONG        0x01    /* l: long or double */
#define LONGDBL     0x02    /* L: long double */
#define SHORT       0x04    /* h: short */
#define SUPPRESS    0x08    /* *: suppress assignment */
#define POINTER     0x10    /* p: void * (as hex) */
#define NOSKIP      0x20    /* [ or c: do not skip blanks */
#define LONGLONG    0x400   /* ll: long long (+ deprecated q: quad) */
#define INTMAXT     0x800   /* j: intmax_t */
#define PTRDIFFT    0x1000  /* t: ptrdiff_t */
#define SIZET       0x2000  /* z: size_t */
#define SHORTSHORT  0x4000  /* hh: char */
#define UNSIGNED    0x8000  /* %[oupxX] conversions */

/*
 * The following are used in integral conversions only:
 * SIGNOK, NDIGITS, PFXOK, and NZDIGITS
 */
#define SIGNOK      0x40    /* +/- is (still) legal */
#define NDIGITS     0x80    /* no digits detected */
#define PFXOK       0x100   /* 0x prefix is (still) legal */
#define NZDIGITS    0x200   /* no zero digits detected */
#define HAVESIGN    0x10000 /* sign detected */

/*
 * Conversion types.
 */
#define CT_CHAR     0   /* %c conversion */
#define CT_CCL      1   /* %[...] conversion */
#define CT_STRING   2   /* %s conversion */
#define CT_INT      3   /* %[dioupxX] conversion */
#define CT_FLOAT    4   /* %[efgEFG] conversion */

#undef __inline
#define __inline

static const u_char *__sccl(char *, const u_char *);
#ifndef NO_FLOATING_POINT
static int parsefloat(unifycr_stream_t *, char *, char *);
#endif

/*
 * Conversion functions are passed a pointer to this object instead of
 * a real parameter to indicate that the assignment-suppression (*)
 * flag was specified.  We could use a NULL pointer to indicate this,
 * but that would mask bugs in applications that call scanf() with a
 * NULL pointer.
 */
static const int suppress;
#define SUPPRESS_PTR    ((void *)&suppress)

static int __srefill(unifycr_stream_t *stream)
{
    /* lookup stream */
    unifycr_stream_t *s = (unifycr_stream_t *) stream;

    /* get pointer to file descriptor structure */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(s->fd);
    if (filedesc == NULL) {
        /* ERROR: invalid file descriptor */
        s->err = 1;
        errno = EBADF;
        return 1;
    }

    /* bail with error if stream not open for reading */
    if (! filedesc->read) {
        s->err = 1;
        errno = EBADF;
        return 1;
    }

    /* associate buffer with stream if we need to */
    if (s->buf == NULL) {
        int setvbuf_rc = unifycr_setvbuf((FILE *)stream, NULL, s->buftype,
                                         UNIFYCR_STREAM_BUFSIZE);
        if (setvbuf_rc != UNIFYCR_SUCCESS) {
            /* ERROR: failed to associate buffer */
            s->err = 1;
            errno = unifycr_err_map_to_errno(setvbuf_rc);
            return 1;
        }
    }

    /* if we have anything on the push back buffer, that must be
     * our current file pointer, since any seek would have cleared
     * the buffer and each read/write/unget keeps it up-to-date */
    if (s->ubuflen > 0) {
        s->_p = s->ubuf + s->ubufsize - s->ubuflen;
        s->_r = s->ubuflen;
        return 1;
    }

    /* check that current falls within buffer */
    off_t current = filedesc->pos;
    off_t start  = s->bufpos;
    off_t length = s->buflen;
    if (current < start || current >= start + length) {
        /* current is outside the range of our buffer */

        /* flush buffer if needed before read */
        int flush_rc = unifycr_stream_flush((FILE *)stream);
        if (flush_rc != UNIFYCR_SUCCESS) {
            /* ERROR: flush sets error indicator and errno */
            return 1;
        }

        /* read data from file into buffer */
        size_t bufcount;
        int read_rc = unifycr_fd_read(s->fd, current, s->buf, s->bufsize, &bufcount);
        if (read_rc != UNIFYCR_SUCCESS) {
            /* ERROR: read error, set error indicator and errno */
            s->err = 1;
            errno = unifycr_err_map_to_errno(read_rc);
            return 1;
        }

        /* record new buffer range within file */
        s->bufpos = current;
        s->buflen = bufcount;
    }

    /* determine number of bytes to copy from stream buffer */
    size_t stream_offset    = (size_t)(current - s->bufpos);
    size_t stream_remaining = s->buflen - stream_offset;
    unsigned char *stream_start = (unsigned char *)s->buf + stream_offset;
    s->_p = stream_start;
    s->_r = stream_remaining;

    if (stream_remaining == 0) {
        return 1;
    }

    return 0;
}

/*
 * The following conversion functions return the number of characters consumed,
 * or -1 on input failure.  Character class conversion returns 0 on match
 * failure.
 */

static __inline int
convert_char(unifycr_stream_t *fp, char *p, int width)
{
    int n;

    if (p == SUPPRESS_PTR) {
        size_t sum = 0;
        for (;;) {
            if ((n = fp->_r) < width) {
                sum += n;
                width -= n;
                fp->_p += n;
                if (__srefill(fp)) {
                    if (sum == 0) {
                        return (-1);
                    }
                    break;
                }
            } else {
                sum += width;
                fp->_r -= width;
                fp->_p += width;
                break;
            }
        }
        return (sum);
    } else {
        size_t r = fread(p, 1, width, (FILE *)fp);

        if (r == 0) {
            return (-1);
        }
        return (r);
    }
}

static __inline int
convert_ccl(unifycr_stream_t *fp, char *p, int width, const char *ccltab)
{
    char *p0;
    int n;

    if (p == SUPPRESS_PTR) {
        n = 0;
        while (ccltab[*fp->_p]) {
            n++, fp->_r--, fp->_p++;
            if (--width == 0) {
                break;
            }
            if (fp->_r <= 0 && __srefill(fp)) {
                break;
            }
        }
    } else {
        p0 = p;
        while (ccltab[*fp->_p]) {
            fp->_r--;
            *p++ = *fp->_p++;
            if (--width == 0) {
                break;
            }
            if (fp->_r <= 0 && __srefill(fp)) {
                if (p == p0) {
                    return (-1);
                }
                break;
            }
        }
        n = p - p0;
        if (n == 0) {
            return (0);
        }
        *p = 0;
    }
    return (n);
}

static __inline int
convert_string(unifycr_stream_t *fp, char *p, int width)
{
    char *p0;
    int n;

    if (p == SUPPRESS_PTR) {
        n = 0;
        while (!isspace(*fp->_p)) {
            n++, fp->_r--, fp->_p++;
            if (--width == 0) {
                break;
            }
            if (fp->_r <= 0 && __srefill(fp)) {
                break;
            }
        }
    } else {
        p0 = p;
        while (!isspace(*fp->_p)) {
            fp->_r--;
            *p++ = *fp->_p++;
            if (--width == 0) {
                break;
            }
            if (fp->_r <= 0 && __srefill(fp)) {
                break;
            }
        }
        *p = 0;
        n = p - p0;
    }
    return (n);
}

/*
 * Read an integer, storing it in buf.  The only relevant bit in the
 * flags argument is PFXOK.
 *
 * Return 0 on a match failure, and the number of characters read
 * otherwise.
 */
static __inline int
parseint(unifycr_stream_t *fp, char *__restrict buf, int width, int base,
         int flags)
{
    /* `basefix' is used to avoid `if' tests */
    static const short basefix[17] =
    { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    char *p;
    int c;

    flags |= SIGNOK | NDIGITS | NZDIGITS;
    for (p = buf; width; width--) {
        c = *fp->_p;
        /*
         * Switch on the character; `goto ok' if we accept it
         * as a part of number.
         */
        switch (c) {

        /*
         * The digit 0 is always legal, but is special.  For
         * %i conversions, if no digits (zero or nonzero) have
         * been scanned (only signs), we will have base==0.
         * In that case, we should set it to 8 and enable 0x
         * prefixing.  Also, if we have not scanned zero
         * digits before this, do not turn off prefixing
         * (someone else will turn it off if we have scanned
         * any nonzero digits).
         */
        case '0':
            if (base == 0) {
                base = 8;
                flags |= PFXOK;
            }
            if (flags & NZDIGITS) {
                flags &= ~(SIGNOK | NZDIGITS | NDIGITS);
            } else {
                flags &= ~(SIGNOK | PFXOK | NDIGITS);
            }
            goto ok;

        /* 1 through 7 always legal */
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
            base = basefix[base];
            flags &= ~(SIGNOK | PFXOK | NDIGITS);
            goto ok;

        /* digits 8 and 9 ok iff decimal or hex */
        case '8':
        case '9':
            base = basefix[base];
            if (base <= 8) {
                break;    /* not legal here */
            }
            flags &= ~(SIGNOK | PFXOK | NDIGITS);
            goto ok;

        /* letters ok iff hex */
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            /* no need to fix base here */
            if (base <= 10) {
                break;    /* not legal here */
            }
            flags &= ~(SIGNOK | PFXOK | NDIGITS);
            goto ok;

        /* sign ok only as first character */
        case '+':
        case '-':
            if (flags & SIGNOK) {
                flags &= ~SIGNOK;
                flags |= HAVESIGN;
                goto ok;
            }
            break;

        /*
         * x ok iff flag still set & 2nd char (or 3rd char if
         * we have a sign).
         */
        case 'x':
        case 'X':
            if (flags & PFXOK && p ==
                buf + 1 + !!(flags & HAVESIGN)) {
                base = 16;  /* if %i */
                flags &= ~PFXOK;
                goto ok;
            }
            break;
        }

        /*
         * If we got here, c is not a legal character for a
         * number.  Stop accumulating digits.
         */
        break;
ok:
        /*
         * c is legal: store it and look at the next.
         */
        *p++ = c;
        if (--fp->_r > 0) {
            fp->_p++;
        } else if (__srefill(fp)) {
            break;    /* EOF */
        }
    }
    /*
     * If we had only a sign, it is no good; push back the sign.
     * If the number ends in `x', it was [sign] '0' 'x', so push
     * back the x and treat it as [sign] '0'.
     */
    if (flags & NDIGITS) {
        if (p > buf)
            (void) ungetc(*(u_char *)--p, (FILE *)fp);
        return (0);
    }
    c = ((u_char *)p)[-1];
    if (c == 'x' || c == 'X') {
        --p;
        (void) ungetc(c, (FILE *)fp);
    }
    return (p - buf);
}

/*
 * __svfscanf - non-MT-safe version of __vfscanf
 */
static int
__svfscanf(unifycr_stream_t *fp, const char *fmt0, va_list ap)
{
#define GETARG(type)    ((flags & SUPPRESS) ? SUPPRESS_PTR : va_arg(ap, type))
    const u_char *fmt = (const u_char *)fmt0;
    int c;          /* character from format, or conversion */
    size_t width;       /* field width, or 0 */
    int flags;      /* flags as defined above */
    int nassigned;      /* number of fields assigned */
    int nconversions;   /* number of conversions */
    int nr;         /* characters read by the current conversion */
    int nread;      /* number of characters consumed from fp */
    int base;       /* base argument to conversion function */
    char ccltab[256];   /* character class table for %[...] */
    char buf[BUF];      /* buffer for numeric conversions */

    nassigned = 0;
    nconversions = 0;
    nread = 0;
    for (;;) {
        c = *fmt++;
        if (c == 0) {
            return (nassigned);
        }
        if (isspace(c)) {
            while ((fp->_r > 0 || __srefill(fp) == 0) && isspace(*fp->_p)) {
                nread++, fp->_r--, fp->_p++;
            }
            continue;
        }
        if (c != '%') {
            goto literal;
        }
        width = 0;
        flags = 0;
        /*
         * switch on the format.  continue if done;
         * break once format type is derived.
         */
again:
        c = *fmt++;
        switch (c) {
        case '%':
literal:
            if (fp->_r <= 0 && __srefill(fp)) {
                goto input_failure;
            }
            if (*fp->_p != c) {
                goto match_failure;
            }
            fp->_r--, fp->_p++;
            nread++;
            continue;

        case '*':
            flags |= SUPPRESS;
            goto again;
        case 'j':
            flags |= INTMAXT;
            goto again;
        case 'l':
            if (flags & LONG) {
                flags &= ~LONG;
                flags |= LONGLONG;
            } else {
                flags |= LONG;
            }
            goto again;
        case 'q':
            flags |= LONGLONG;  /* not quite */
            goto again;
        case 't':
            flags |= PTRDIFFT;
            goto again;
        case 'z':
            flags |= SIZET;
            goto again;
        case 'L':
            flags |= LONGDBL;
            goto again;
        case 'h':
            if (flags & SHORT) {
                flags &= ~SHORT;
                flags |= SHORTSHORT;
            } else {
                flags |= SHORT;
            }
            goto again;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            width = width * 10 + c - '0';
            goto again;

        /*
         * Conversions.
         */
        case 'd':
            c = CT_INT;
            base = 10;
            break;

        case 'i':
            c = CT_INT;
            base = 0;
            break;

        case 'o':
            c = CT_INT;
            flags |= UNSIGNED;
            base = 8;
            break;

        case 'u':
            c = CT_INT;
            flags |= UNSIGNED;
            base = 10;
            break;

        case 'X':
        case 'x':
            flags |= PFXOK; /* enable 0x prefixing */
            c = CT_INT;
            flags |= UNSIGNED;
            base = 16;
            break;

#ifndef NO_FLOATING_POINT
        case 'A':
        case 'E':
        case 'F':
        case 'G':
        case 'a':
        case 'e':
        case 'f':
        case 'g':
            c = CT_FLOAT;
            break;
#endif

        case 'S':
            flags |= LONG;
        /* FALLTHROUGH */
        case 's':
            c = CT_STRING;
            break;

        case '[':
            fmt = __sccl(ccltab, fmt);
            flags |= NOSKIP;
            c = CT_CCL;
            break;

        case 'C':
            flags |= LONG;
        /* FALLTHROUGH */
        case 'c':
            flags |= NOSKIP;
            c = CT_CHAR;
            break;

        case 'p':   /* pointer format is like hex */
            flags |= POINTER | PFXOK;
            c = CT_INT;     /* assumes sizeof(uintmax_t) */
            flags |= UNSIGNED;  /*      >= sizeof(uintptr_t) */
            base = 16;
            break;

        case 'n':
            if (flags & SUPPRESS) { /* ??? */
                continue;
            }
            if (flags & SHORTSHORT) {
                *va_arg(ap, char *) = nread;
            } else if (flags & SHORT) {
                *va_arg(ap, short *) = nread;
            } else if (flags & LONG) {
                *va_arg(ap, long *) = nread;
            } else if (flags & LONGLONG) {
                *va_arg(ap, long long *) = nread;
            } else if (flags & INTMAXT) {
                *va_arg(ap, intmax_t *) = nread;
            } else if (flags & SIZET) {
                *va_arg(ap, size_t *) = nread;
            } else if (flags & PTRDIFFT) {
                *va_arg(ap, ptrdiff_t *) = nread;
            } else {
                *va_arg(ap, int *) = nread;
            }
            continue;

        default:
            goto match_failure;

        /*
         * Disgusting backwards compatibility hack. XXX
         */
        case '\0':  /* compat */
            return (EOF);
        }

        /*
         * We have a conversion that requires input.
         */
        if (fp->_r <= 0 && __srefill(fp)) {
            goto input_failure;
        }

        /*
         * Consume leading white space, except for formats
         * that suppress this.
         */
        if ((flags & NOSKIP) == 0) {
            while (isspace(*fp->_p)) {
                nread++;
                if (--fp->_r > 0) {
                    fp->_p++;
                } else if (__srefill(fp)) {
                    goto input_failure;
                }
            }
            /*
             * Note that there is at least one character in
             * the buffer, so conversions that do not set NOSKIP
             * ca no longer result in an input failure.
             */
        }

        /*
         * Do the conversion.
         */
        nr = 0;
        switch (c) {

        case CT_CHAR:
            /* scan arbitrary characters (sets NOSKIP) */
            if (width == 0) {
                width = 1;
            }
            if (flags & LONG) {
                unifycr_unsupported_stream((FILE *)fp, __func__, __FILE__,
                                           __LINE__, "%s", fmt0);
            } else {
                nr = convert_char(fp, GETARG(char *), width);
                if (nr < 0)
                    goto input_failure;
            }
            break;

        case CT_CCL:
            /* scan a (nonempty) character class (sets NOSKIP) */
            if (width == 0) {
                width = (size_t)~0;    /* `infinity' */
            }
            if (flags & LONG) {
                unifycr_unsupported_stream((FILE *)fp, __func__, __FILE__,
                                           __LINE__, "%s", fmt0);
            } else {
                nr = convert_ccl(fp, GETARG(char *), width,
                                 ccltab);
                if (nr <= 0) {
                    if (nr < 0)
                        goto input_failure;
                    else /* nr == 0 */
                        goto match_failure;
                }
            }
            break;

        case CT_STRING:
            /* like CCL, but zero-length string OK, & no NOSKIP */
            if (width == 0) {
                width = (size_t)~0;
            }
            if (flags & LONG) {
                unifycr_unsupported_stream((FILE *)fp, __func__, __FILE__,
                                           __LINE__, "%s", fmt0);
            } else {
                nr = convert_string(fp, GETARG(char *), width);
                if (nr < 0)
                    goto input_failure;
            }
            break;

        case CT_INT:
            /* scan an integer as if by the conversion function */
#ifdef hardway
            if (width == 0 || width > sizeof(buf) - 1) {
                width = sizeof(buf) - 1;
            }
#else
            /* size_t is unsigned, hence this optimisation */
            if (--width > sizeof(buf) - 2) {
                width = sizeof(buf) - 2;
            }
            width++;
#endif
            nr = parseint(fp, buf, width, base, flags);
            if (nr == 0) {
                goto match_failure;
            }
            if ((flags & SUPPRESS) == 0) {
                uintmax_t res;

                buf[nr] = '\0';
                if ((flags & UNSIGNED) == 0) {
                    res = strtoimax(buf, (char **)NULL, base);
                } else {
                    res = strtoumax(buf, (char **)NULL, base);
                }
                if (flags & POINTER)
                    *va_arg(ap, void **) =
                        (void *)(uintptr_t)res;
                else if (flags & SHORTSHORT) {
                    *va_arg(ap, char *) = res;
                } else if (flags & SHORT) {
                    *va_arg(ap, short *) = res;
                } else if (flags & LONG) {
                    *va_arg(ap, long *) = res;
                } else if (flags & LONGLONG) {
                    *va_arg(ap, long long *) = res;
                } else if (flags & INTMAXT) {
                    *va_arg(ap, intmax_t *) = res;
                } else if (flags & PTRDIFFT) {
                    *va_arg(ap, ptrdiff_t *) = res;
                } else if (flags & SIZET) {
                    *va_arg(ap, size_t *) = res;
                } else {
                    *va_arg(ap, int *) = res;
                }
            }
            break;

#ifndef NO_FLOATING_POINT
        case CT_FLOAT:
            /* scan a floating point number as if by strtod */
            if (width == 0 || width > sizeof(buf) - 1) {
                width = sizeof(buf) - 1;
            }
            nr = parsefloat(fp, buf, buf + width);
            if (nr == 0) {
                goto match_failure;
            }
            if ((flags & SUPPRESS) == 0) {
                if (flags & LONGDBL) {
                    long double res = strtold(buf, NULL);
                    *va_arg(ap, long double *) = res;
                } else if (flags & LONG) {
                    double res = strtod(buf, NULL);
                    *va_arg(ap, double *) = res;
                } else {
                    float res = strtof(buf, NULL);
                    *va_arg(ap, float *) = res;
                }
            }
            break;
#endif /* !NO_FLOATING_POINT */
        }
        if (!(flags & SUPPRESS)) {
            nassigned++;
        }
        nread += nr;
        nconversions++;
    }
input_failure:
    return (nconversions != 0 ? nassigned : EOF);
match_failure:
    return (nassigned);
}

/*
 * Fill in the given table from the scanset at the given format
 * (just after `[').  Return a pointer to the character past the
 * closing `]'.  The table has a 1 wherever characters should be
 * considered part of the scanset.
 */
static const u_char *
__sccl(tab, fmt)
char *tab;
const u_char *fmt;
{
    int c, n, v;

    /* first `clear' the whole table */
    c = *fmt++;     /* first char hat => negated scanset */
    if (c == '^') {
        v = 1;      /* default => accept */
        c = *fmt++; /* get new first char */
    } else {
        v = 0;    /* default => reject */
    }

    /* XXX: Will not work if sizeof(tab*) > sizeof(char) */
    (void) memset(tab, v, 256);

    if (c == 0) {
        return (fmt - 1);    /* format ended before closing ] */
    }

    /*
     * Now set the entries corresponding to the actual scanset
     * to the opposite of the above.
     *
     * The first character may be ']' (or '-') without being special;
     * the last character may be '-'.
     */
    v = 1 - v;
    for (;;) {
        tab[c] = v;     /* take character c */
doswitch:
        n = *fmt++;     /* and examine the next */
        switch (n) {

        case 0:         /* format ended too soon */
            return (fmt - 1);

        case '-':
            /*
             * A scanset of the form
             *  [01+-]
             * is defined as `the digit 0, the digit 1,
             * the character +, the character -', but
             * the effect of a scanset such as
             *  [a-zA-Z0-9]
             * is implementation defined.  The V7 Unix
             * scanf treats `a-z' as `the letters a through
             * z', but treats `a-a' as `the letter a, the
             * character -, and the letter a'.
             *
             * For compatibility, the `-' is not considerd
             * to define a range if the character following
             * it is either a close bracket (required by ANSI)
             * or is not numerically greater than the character
             * we just stored in the table (c).
             */
            n = *fmt;
            if (n == ']' || n < c) {
                c = '-';
                break;  /* resume the for(;;) */
            }
            fmt++;
            /* fill in the range */
            do {
                tab[++c] = v;
            } while (c < n);
            c = n;
            /*
             * Alas, the V7 Unix scanf also treats formats
             * such as [a-c-e] as `the letters a through e'.
             * This too is permitted by the standard....
             */
            goto doswitch;
            break;

        case ']':       /* end of scanset */
            return (fmt);

        default:        /* just another character */
            c = n;
            break;
        }
    }
/* NOTREACHED */
}

#ifndef NO_FLOATING_POINT
static int
//parsefloat(unifycr_stream_t *fp, char *buf, char *end, locale_t locale)
parsefloat(unifycr_stream_t *fp, char *buf, char *end)
{
    char *commit, *p;
    int infnanpos = 0, decptpos = 0;
    enum {
        S_START, S_GOTSIGN, S_INF, S_NAN, S_DONE, S_MAYBEHEX,
        S_DIGITS, S_DECPT, S_FRAC, S_EXP, S_EXPDIGITS
    } state = S_START;
    unsigned char c;
    const char us_decpt[] = ".";
    const char *decpt = us_decpt;
    int gotmantdig = 0, ishex = 0;

    /*
     * We set commit = p whenever the string we have read so far
     * constitutes a valid representation of a floating point
     * number by itself.  At some point, the parse will complete
     * or fail, and we will ungetc() back to the last commit point.
     * To ensure that the file offset gets updated properly, it is
     * always necessary to read at least one character that doesn't
     * match; thus, we can't short-circuit "infinity" or "nan(...)".
     */
    commit = buf - 1;
    for (p = buf; p < end;) {
        c = *fp->_p;
reswitch:
        switch (state) {
        case S_START:
            state = S_GOTSIGN;
            if (c == '-' || c == '+') {
                break;
            } else {
                goto reswitch;
            }
        case S_GOTSIGN:
            switch (c) {
            case '0':
                state = S_MAYBEHEX;
                commit = p;
                break;
            case 'I':
            case 'i':
                state = S_INF;
                break;
            case 'N':
            case 'n':
                state = S_NAN;
                break;
            default:
                state = S_DIGITS;
                goto reswitch;
            }
            break;
        case S_INF:
            if (infnanpos > 6 ||
                (c != "nfinity"[infnanpos] &&
                 c != "NFINITY"[infnanpos])) {
                goto parsedone;
            }
            if (infnanpos == 1 || infnanpos == 6) {
                commit = p;    /* inf or infinity */
            }
            infnanpos++;
            break;
        case S_NAN:
            switch (infnanpos) {
            case 0:
                if (c != 'A' && c != 'a') {
                    goto parsedone;
                }
                break;
            case 1:
                if (c != 'N' && c != 'n') {
                    goto parsedone;
                } else {
                    commit = p;
                }
                break;
            case 2:
                if (c != '(') {
                    goto parsedone;
                }
                break;
            default:
                if (c == ')') {
                    commit = p;
                    state = S_DONE;
                } else if (!isalnum(c) && c != '_') {
                    goto parsedone;
                }
                break;
            }
            infnanpos++;
            break;
        case S_DONE:
            goto parsedone;
        case S_MAYBEHEX:
            state = S_DIGITS;
            if (c == 'X' || c == 'x') {
                ishex = 1;
                break;
            } else {    /* we saw a '0', but no 'x' */
                gotmantdig = 1;
                goto reswitch;
            }
        case S_DIGITS:
            if ((ishex && isxdigit(c)) || isdigit(c)) {
                gotmantdig = 1;
                commit = p;
                break;
            } else {
                state = S_DECPT;
                goto reswitch;
            }
        case S_DECPT:
            if (c == decpt[decptpos]) {
                if (decpt[++decptpos] == '\0') {
                    /* We read the complete decpt seq. */
                    state = S_FRAC;
                    if (gotmantdig) {
                        commit = p;
                    }
                }
                break;
            } else if (!decptpos) {
                /* We didn't read any decpt characters. */
                state = S_FRAC;
                goto reswitch;
            } else {
                /*
                 * We read part of a multibyte decimal point,
                 * but the rest is invalid, so bail.
                 */
                goto parsedone;
            }
        case S_FRAC:
            if (((c == 'E' || c == 'e') && !ishex) ||
                ((c == 'P' || c == 'p') && ishex)) {
                if (!gotmantdig) {
                    goto parsedone;
                } else {
                    state = S_EXP;
                }
            } else if ((ishex && isxdigit(c)) || isdigit(c)) {
                commit = p;
                gotmantdig = 1;
            } else {
                goto parsedone;
            }
            break;
        case S_EXP:
            state = S_EXPDIGITS;
            if (c == '-' || c == '+') {
                break;
            } else {
                goto reswitch;
            }
        case S_EXPDIGITS:
            if (isdigit(c)) {
                commit = p;
            } else {
                goto parsedone;
            }
            break;
        default:
            abort();
        }
        *p++ = c;
        if (--fp->_r > 0) {
            fp->_p++;
        } else if (__srefill(fp)) {
            break;    /* EOF */
        }
    }

parsedone:
    while (commit < --p)
        ungetc(*(u_char *)p, (FILE *)fp);
    *++commit = '\0';
    return (commit - buf);
}
#endif
