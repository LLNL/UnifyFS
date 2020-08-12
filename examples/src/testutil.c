/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <sys/sysmacros.h> /* device major() and minor() macros */

#include "testutil.h"

/* Clone of the apsrintf().  See the standard asprintf() man page for details */
static int asprintf(char** strp, const char* fmt, ...)
{
    /*
     * This code is taken from the vmalloc(3) man page and modified slightly.
     */
    int n;
    int size = 100;     /* Guess we need no more than 100 bytes */
    char* p;
    char* np;
    va_list ap;

    p = malloc(size);
    if (p == NULL) {
        *strp = NULL;
        return -ENOMEM;
    }

    while (1) {
        /* Try to print in the allocated space */

        va_start(ap, fmt);
        n = vsnprintf(p, size, fmt, ap);
        va_end(ap);

        /* Check error code */

        if (n < 0) {
            *strp = NULL;
            return -1;
        }

        /* If that worked, return the string */
        if (n < size) {
            *strp = p;
            return n;
        }

        /* Else try again with more space */

        size = n + 1;       /* Precisely what is needed */

        np = realloc(p, size);
        if (np == NULL) {
            *strp = NULL;
            free(p);
            return -ENOMEM;
        } else {
            p = np;
        }
    }
}


/******************************************************************************
 * Below are some standard unix commands replicated in C.  These were included
 * to make testing easier, since you can really do a lot with just a handful
 * of commands.  All these are named 'unix name'_cmd() to differentiate the
 * command from the system calls by the same name (like "stat").
 *****************************************************************************/

/*
 * C clone of 'dd'.  All semantics are the same.
 * Return 0 on success, errno otherwise.
 */
int dd_cmd(test_cfg* cfg, char* infile, char* outfile, unsigned long bs,
    unsigned long count, unsigned long seek)
{
    FILE* infp;
    FILE* outfp;
    char* buf;
    size_t rc;
    int i;
    infp = fopen(infile, "r");
    if (!infp) {
        test_print(cfg, "Error opening infile");
        return errno;
    }
    outfp = fopen(outfile, "w");
    if (!outfp) {
        test_print(cfg, "Error opening outfile");
        return errno;
    }
    buf = calloc(sizeof(*buf), bs);
    if (NULL == buf) {
        return errno;
    }

    rc = fseek(outfp, seek * bs, SEEK_SET);
    if (rc) {
        test_print(cfg, "Error seeking outfile");
        return errno;
    }

    for (i = 0; i < count; i++) {
        if (feof(infp)) {
            break;
        }
        rc = fread(buf, bs, 1, infp);
        if (ferror(infp)) {
            test_print(cfg, "Error reading infile");
            return errno;
        }
        rc = fwrite(buf, rc * bs, 1, outfp);
        if (ferror(outfp)) {
            test_print(cfg, "Error writing outfile");
            return errno;
        }
    }
    free(buf);
    fclose(infp);
    fclose(outfp);
    return 0;
}

/*
 * C clone of the 'test' command.  Only supports file checks like
 * "test -f $file" or "test -d $dir" for now.
 */
int test_cmd(char* expression, char* path)
{
    struct stat sb;
    int ret;

    ret = stat(path, &sb);
    /*
     * We're stat'ing a file/dir which may or may not exist.  Clear errno so
     * that we don't pollute future test_print() calls with bogus errors.
     */
    errno = 0;
    if (ret) {
        return 0;
    }

    ret = 0;
    switch (sb.st_mode & S_IFMT) {
    case S_IFREG:
        if (strcmp(expression, "-f") == 0) {
            ret = 1;
        }
        break;
    case S_IFDIR:
        if (strcmp(expression, "-d") == 0) {
            ret = 1;
        }
        break;
    default:
        break;
    }
    return ret;
}

/*
 * C clone of 'mktemp'.  tmpdir is the directory to create the file in.
 *
 * Makes the file with zero length and returns the filename.  The filename
 * is malloc'd so it must be freed when finished.
 */
char* mktemp_cmd(test_cfg* cfg, char* tmpdir)
{
    char* tmpfile = NULL;
    int i;
    char letters[11];
    char r;
    char* job_id;
    if (!tmpdir) {
        tmpdir = "/tmp";
    }

    if (!test_cmd("-d", tmpdir)) {
        test_print(cfg, "%s dir does not exists", tmpdir);
        return NULL;
    }

    /* If we can get our job ID, use it to seed our RNG. Else use our app_id */
    job_id = getenv("LSB_JOBID");
    if (job_id) {
        srand(atol(job_id));
    } else {
        srand(cfg->app_id);
    }

    do {
        /*
         * The normal mktemp creates filenames like /tmp/tmp.qTHxL7SLGx. Let's
         * try to replicate that.
         */
        i = 0;
        do {
            /*
             * First make our qTHxL7SLGx string.  Pick random characters that
             * are alphanumeric.
             */
            r = rand() % 123;   /* Ignore anything past char 122 ('z') */
            if (isalnum(r)) {
                letters[i] = r;
                i++;
            }
        } while (i < 10);
        letters[10] = '\0';

        asprintf(&tmpfile, "%s/tmp.%s", tmpdir, letters);
        if (!tmpfile) {
            return NULL;
        }

    /* Loop until we've generated a filename that doesn't already exist */
    } while (test_cmd("-f", tmpfile));

    /*
     * Success, we've generated a tmpfile name that doesn't already exist.
     * Make an empty file (use bs=1 to follow normal dd semantics).
     */
    test_print(cfg, "File doesn't exist, create it");
    if (dd_cmd(cfg, "/dev/zero", tmpfile, 1, 0, 0) != 0) {
        return NULL;
    }

    return tmpfile;
}

/* C clone of 'du'.  Returns the logical file size unless apparent_size = 1,
 * in which case it returns the apparent size (aka the "log size" in UnifyFS
 * speak). Otherwise, return a negative number on error.
 */
long du_cmd(test_cfg* cfg, char* filename, int apparent_size)
{
    struct stat sb;
    if (stat(filename, &sb) == -1) {
        test_print(cfg, "Error stat");
        return -1;
    }

    if (apparent_size) {
        return sb.st_blksize * sb.st_blocks;
    } else {
        return sb.st_size;
    }
}

/*  C clone of 'sync [filename]' */
int sync_cmd(test_cfg* cfg, char* filename)
{
    int fd;
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        test_print(cfg, "Error opening file");
        return errno;
    }
    if (fsync(fd) != 0) {
        test_print(cfg, "Error syncing file");
        return errno;
    }
    close(fd);
}

/* C clone of 'stat' */
int stat_cmd(test_cfg* cfg, char* filename)
{
    struct stat sb;
    time_t timestamp;
    int rc;
    const char* typestr;
    char* tmp;
    char datestr[32];

    rc = stat(filename, &sb);
    if (rc) {
        test_print(cfg, "ERROR: stat(%s) - %s", filename, strerror(rc));
        return rc;
    }

    switch (sb.st_mode & S_IFMT) {
    case S_IFREG:
        typestr = "regular file";
        break;
    case S_IFDIR:
        typestr = "directory";
        break;
    case S_IFCHR:
        typestr = "character device";
        break;
    case S_IFBLK:
        typestr = "block device";
        break;
    case S_IFLNK:
        typestr = "symbolic (soft) link";
        break;
    case S_IFIFO:
        typestr = "FIFO or pipe";
        break;
    case S_IFSOCK:
        typestr = "socket";
        break;
    default:
        typestr = "unknown file type?";
        break;
    }

    /* Do some work to get the ':' in the right place */
    asprintf(&tmp, "%s:", filename);
    test_print(cfg, "%-26s%s", tmp, typestr);
    free(tmp);

    test_print(cfg, "Device containing i-node:  major=%ld   minor=%ld",
           (long) major(sb.st_dev), (long) minor(sb.st_dev));

    test_print(cfg, "I-node number:            %lu",
        (unsigned long) sb.st_ino);

    test_print(cfg, "Mode:                     %lo",
           (unsigned long) sb.st_mode);

    if (sb.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) {
        test_print(cfg, "    special bits set:     %s%s%s",
               (sb.st_mode & S_ISUID) ? "set-UID " : "",
               (sb.st_mode & S_ISGID) ? "set-GID " : "",
               (sb.st_mode & S_ISVTX) ? "sticky " : "");
    }

    test_print(cfg, "Number of (hard) links:   %lu",
        (unsigned long) sb.st_nlink);

    test_print(cfg, "Ownership:                UID=%ld   GID=%ld",
           (long) sb.st_uid, (long) sb.st_gid);

    if (S_ISCHR(sb.st_mode) || S_ISBLK(sb.st_mode)) {
        test_print(cfg, "Device number (st_rdev):  major=%ld; minor=%ld",
               (long) major(sb.st_rdev), (long) minor(sb.st_rdev));
    }

    test_print(cfg, "File size:                %llu bytes",
        (unsigned long long) sb.st_size);
    test_print(cfg, "Optimal I/O block size:   %lu bytes",
        (unsigned long) sb.st_blksize);
    test_print(cfg, "Blocks allocated:         %llu",
        (unsigned long long) sb.st_blocks);

    memset(datestr, 0, sizeof(datestr));
    timestamp = sb.st_atime;
    ctime_r(&timestamp, datestr);
    test_print(cfg, "Last file access:         %s", datestr);

    memset(datestr, 0, sizeof(datestr));
    timestamp = sb.st_mtime;
    ctime_r(&timestamp, datestr);
    test_print(cfg, "Last file modification:   %s", datestr);

    memset(datestr, 0, sizeof(datestr));
    timestamp = sb.st_ctime;
    ctime_r(&timestamp, datestr);
    test_print(cfg, "Last status change:       %s", datestr);
}
