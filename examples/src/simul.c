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

/*
 * Copyright (C) 2003, The Regents of the University of California.
 *  Produced at the Lawrence Livermore National Laboratory.
 *  Written by Christopher J. Morrone <morrone@llnl.gov>
 *  UCRL-CODE-2003-019
 *  All rights reserved.
 */

/*
 * Some modifications including style changes have been made for testing
 * unifyfs:
 * To test with UnifyFS, pass the '-u' flag. Then, simul will set the test
 * directory as /unifyfs, where unifyfs will be accordingly mounted.
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

/* unifyfs test */
#include <unifyfs.h>

#define FILEMODE S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH
#define DIRMODE S_IRUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IXOTH
#define SHARED 1
#define MAX_FILENAME_LEN 512

int rank;
int size;
char *testdir = NULL;
char hostname[1024];
int verbose;
int throttle = 1;
struct timeval t1, t2;
static char version[] = "1.16";

#ifdef __GNUC__
   /* "inline" is a keyword in GNU C */
#elif __STDC_VERSION__ >= 199901L
   /* "inline" is a keyword in C99 and later versions */
#else
#  define inline /* "inline" not available */
#endif

#ifndef AIX
#define FAIL(msg) do { \
    fprintf(stdout, "%s: Process %d(%s): FAILED in %s, %s: %s\n",\
        timestamp(), rank, hostname, __func__, \
        msg, strerror(errno)); \
    fflush(stdout);\
    MPI_Abort(MPI_COMM_WORLD, 1); \
} while(0)
#else
#define FAIL(msg) do { \
    fprintf(stdout, "%s: Process %d(%s): FAILED, %s: %s\n",\
        timestamp(), rank, hostname, \
        msg, strerror(errno)); \
    fflush(stdout);\
    MPI_Abort(MPI_COMM_WORLD, 1); \
} while(0)
#endif

char *timestamp() {
    static char datestring[80];
    time_t timestamp;

    fflush(stdout);
    timestamp = time(NULL);
    strftime(datestring, 80, "%T", localtime(&timestamp));

    return datestring;
}

static inline void begin(char *str) {
    if (verbose > 0 && rank == 0) {
        gettimeofday(&t1, NULL);
        fprintf(stdout, "%s:\tBeginning %s\n", timestamp(), str);
        fflush(stdout);
    }
}

static inline void end(char *str) {
    double elapsed;

    MPI_Barrier(MPI_COMM_WORLD);
    if (verbose > 0 && rank == 0) {
        gettimeofday(&t2, NULL);
        elapsed = ((((t2.tv_sec - t1.tv_sec) * 1000000L)
                    + t2.tv_usec) - t1.tv_usec)
            / (double)1000000;
        if (elapsed >= 60) {
            fprintf(stdout, "%s:\tFinished %-15s(%.2f min)\n",
                    timestamp(), str, elapsed / 60);
        } else {
            fprintf(stdout, "%s:\tFinished %-15s(%.3f sec)\n",
                    timestamp(), str, elapsed);

        }
        fflush(stdout);
    }
}

void Seq_begin(MPI_Comm comm, int numprocs) {
    int size;
    int rank;
    int buf;
    MPI_Status status;

    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);

    if (rank >= numprocs) {
        MPI_Recv(&buf, 1, MPI_INT, rank-numprocs, 1333, comm, &status);
    }
}

void Seq_end(MPI_Comm comm, int numprocs) {
    int size;
    int rank;
    int buf;

    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);

    if ((rank + numprocs) < size) {
        MPI_Send(&buf, 1, MPI_INT, rank+numprocs, 1333, comm);
    }
}

/* This function does not FAIL if the requested "name" does not exist.  This
   is just to clean up any files or directories left over from previous runs.*/
void remove_file_or_dir(char *name) {
    struct stat statbuf;
    char errmsg[MAX_FILENAME_LEN+20];

    if (stat(name, &statbuf) != -1) {
        if (S_ISREG(statbuf.st_mode)) {
            printf("stale file found\n");
            if (unlink(name) == -1) {
                sprintf(errmsg, "unlink of %s", name);
                FAIL(errmsg);
            }
        }
        if (S_ISDIR(statbuf.st_mode)) {
            printf("stale directory found\n");
            if (rmdir(name) == -1) {
                sprintf(errmsg, "rmmdir of %s", name);
                FAIL(errmsg);
            }
        }
    }
}

char *create_files(char *prefix, int filesize, int shared) {
    static char filename[MAX_FILENAME_LEN];
    char errmsg[MAX_FILENAME_LEN+20];
    int fd, i;
    short zero = 0;

    /* Process 0 creates the test file(s) */
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            sprintf(filename, "%s/%s.%d", testdir, prefix, i);
            remove_file_or_dir(filename);
            if ((fd = creat(filename, FILEMODE)) == -1) {
                sprintf(errmsg, "creat of file %s", filename);
                FAIL(errmsg);
            }
            if (filesize > 0) {
                if (lseek(fd, filesize - 1, SEEK_SET) == -1) {
                    sprintf(errmsg, "lseek in file %s", filename);
                    FAIL(errmsg);
                }
                if (write(fd, &zero, 1) == -1) {
                    sprintf(errmsg, "write in file %s", filename);
                    FAIL(errmsg);
                }
            }
            if (close(fd) == -1) {
                sprintf(errmsg, "close of file %s", filename);
                FAIL(errmsg);
            }
        }
    }

    if (shared)
        sprintf(filename, "%s/%s.0", testdir, prefix);
    else
        sprintf(filename, "%s/%s.%d", testdir, prefix, rank);

    return filename;
}

void remove_files(char *prefix, int shared) {
    char filename[1024];
    int i;

    /* Process 0 removes the file(s) */
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            sprintf(filename, "%s/%s.%d", testdir, prefix, i);
            /*printf("Removing file %s\n", filename); fflush(stdout);*/
            if (unlink(filename) == -1) {
                FAIL("unlink failed");
            }
        }
    }
}

char *create_dirs(char *prefix, int shared) {
    static char dirname[1024];
    int i;

    /* Process 0 creates the test file(s) */
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            sprintf(dirname, "%s/%s.%d", testdir, prefix, i);
            remove_file_or_dir(dirname);
            if (mkdir(dirname, DIRMODE) == -1) {
                FAIL("init mkdir failed");
            }
        }
    }

    if (shared)
        sprintf(dirname, "%s/%s.0", testdir, prefix);
    else
        sprintf(dirname, "%s/%s.%d", testdir, prefix, rank);

    return dirname;
}

void remove_dirs(char *prefix, int shared) {
    char dirname[1024];
    int i;

    /* Process 0 removes the file(s) */
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            sprintf(dirname, "%s/%s.%d", testdir, prefix, i);
            if (rmdir(dirname) == -1) {
                FAIL("rmdir failed");
            }
        }
    }
}

char *create_symlinks(char *prefix, int shared) {
    static char filename[1024];
    static char linkname[1024];
    int i;

    /* Process 0 creates the test file(s) */
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            sprintf(filename, "%s/symlink_target", testdir);
            sprintf(linkname, "%s/%s.%d", testdir, prefix, i);
            remove_file_or_dir(linkname);
            if (symlink(filename, linkname) == -1) {
                FAIL("symlink failed");
            }
        }
    }

    if (shared)
        sprintf(linkname, "%s/%s.0", testdir, prefix);
    else
        sprintf(linkname, "%s/%s.%d", testdir, prefix, rank);

    return linkname;
}

void check_single_success(char *testname, int rc, int error_rc) {
    int *rc_vec, i;
    int fail = 0;
    int pass = 0;

    if (rank == 0) {
        if ((rc_vec = (int *)malloc(sizeof(int)*size)) == NULL) {
            FAIL("malloc failed");
        }
    }
    MPI_Gather(&rc, 1, MPI_INT, rc_vec, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        for (i = 0; i < size; i++) {
            if (rc_vec[i] == error_rc)
                fail++;
            else
                pass++;
        }
        if (!((pass == 1) && (fail == size-1))) {
            fprintf(stdout, "%s: FAILED in %s: ", timestamp(), testname);
            if (pass > 1)
                fprintf(stdout, "too many operations succeeded (%d).\n", pass);
            else
                fprintf(stdout, "too many operations failed (%d).\n", fail);
            fflush(stdout);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        free(rc_vec);
    }
}

void simul_open(int shared) {
    int fd;
    char *filename;

    begin("setup");
    filename = create_files("simul_open", 0, shared);
    end("setup");

    /* All open the file simultaneously */
    begin("test");
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("open failed");
    }
    end("test");

    /* All close the file one at a time */
    begin("cleanup");
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (close(fd) == -1) {
        FAIL("close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_open", shared);
    end("cleanup");
}

void simul_close(int shared) {
    int fd;
    char *filename;

    begin("setup");
    filename = create_files("simul_close", 0, shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("open failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All close the file simultaneously */
    if (close(fd) == -1) {
        FAIL("close failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_close", shared);
    end("cleanup");
}

void simul_chdir(int shared) {
    char cwd[1024];
    char *dirname;

    begin("setup");
    if (getcwd(cwd, 1024) == NULL) {
        FAIL("init getcwd failed");
    }
    dirname = create_dirs("simul_chdir", shared);
    end("setup");

    begin("test");
    /* All chdir to dirname */
    if (chdir(dirname) == -1) {
        FAIL("chdir failed");
    }
    end("test");

    begin("cleanup");
    /* All chdir back to old cwd */
    if (chdir(cwd) == -1) {
        FAIL("chdir back failed");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    remove_dirs("simul_chdir", shared);
    end("cleanup");
}

void simul_file_stat(int shared) {
    char *filename;
    struct stat buf;

    begin("setup");
    filename = create_files("simul_file_stat", 0, shared);
    end("setup");

    begin("test");
    /* All stat the file */
    if (stat(filename, &buf) == -1) {
        FAIL("stat failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_file_stat", shared);
    end("cleanup");
}

void simul_dir_stat(int shared) {
    char *dirname;
    struct stat buf;

    begin("setup");
    dirname = create_dirs("simul_dir_stat", shared);
    end("setup");

    begin("test");
    /* All stat the directory */
    if (stat(dirname, &buf) == -1) {
        FAIL("stat failed");
    }
    end("test");

    begin("cleanup");
    remove_dirs("simul_dir_stat", shared);
    end("cleanup");
}

void simul_readdir(int shared) {
    DIR *dir;
    char *dirname;
    struct dirent *dptr;

    begin("setup");
    dirname = create_dirs("simul_readdir", shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the directory(ies) one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((dir = opendir(dirname)) == NULL) {
        FAIL("init opendir failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All readdir the directory stream(s) */
    if ((dptr = readdir(dir)) == NULL) {
        FAIL("readdir failed");
    }
    end("test");

    begin("cleanup");
    /* All close the directory(ies) one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (closedir(dir) == -1) {
        FAIL("closedir failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_dirs("simul_readdir", shared);
    end("cleanup");
}

void simul_statfs(int shared) {
    char *filename;
    struct statfs buf;

    begin("setup");
    filename = create_files("simul_statfs", 0, shared);
    end("setup");

    begin("test");
    /* All statfs the file(s) */
    if (statfs(filename, &buf) == -1) {
        FAIL("statfs failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_statfs", shared);
    end("cleanup");
}

void simul_lseek(int shared) {
    int fd;
    char *filename;

    begin("setup");
    filename = create_files("simul_lseek", 0, shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the file(s) one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("init open failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All lseek simultaneously */
    if (lseek(fd, 1024, SEEK_SET) == -1) {
        FAIL("lseek failed");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    end("test");

    begin("cleanup");
    /* All close the file(s) one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (close(fd) == -1) {
        FAIL("cleanup close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_lseek", shared);
    end("cleanup");
}

void simul_read(int shared) {
    int fd;
    ssize_t fin;
    char buf[1024];
    char *filename;
    int i = 0;
    int retry = 100;

    begin("setup");
    filename = create_files("simul_read", 1024, shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("init open failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All read simultaneously */
    for (i = 1024; (i > 0) && (retry > 0); i -= fin, retry--) {
        if ((fin = read(fd, buf, (size_t)i)) == -1) {
            FAIL("read failed");
        }
    }
    if( (retry == 0) && (i > 0) )
        FAIL("read exceeded retry count");
    end("test");

    begin("cleanup");
    /* All close the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (close(fd) == -1) {
        FAIL("cleanup close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_read", shared);
    end("cleanup");
}

void simul_write(int shared) {
    int fd;
    ssize_t fin;
    char *filename;
    int i = 0;
    int retry = 100;

    begin("setup");
    filename = create_files("simul_write", size * sizeof(int), shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the file and lseek one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("init open failed");
    }
    if (lseek(fd, rank*sizeof(int), SEEK_SET) == -1) {
        FAIL("init lseek failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All write simultaneously */
    for (i = sizeof(int); (i > 0) && (retry > 0); i -= fin, retry--) {
        if ((fin = write(fd, &rank, (size_t)i)) == -1) {
            FAIL("write failed");
        }
    }
    if( (retry == 0) && (i > 0) )
        FAIL("write exceeded retry count");
    end("test");

    begin("cleanup");
    /* All close the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (close(fd) == -1) {
        FAIL("cleanup close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_write", shared);
    end("cleanup");
}

void simul_mkdir(int shared) {
    int rc, i;
    char dirname[MAX_FILENAME_LEN];

    begin("setup");
    if (shared)
        sprintf(dirname, "%s/simul_mkdir.0", testdir);
    else
        sprintf(dirname, "%s/simul_mkdir.%d", testdir, rank);
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/simul_mkdir.%d", testdir, i);
            remove_file_or_dir(buf);
        }
    }
    end("setup");

    begin("test");
    /* All mkdir dirname */
    rc = mkdir(dirname, DIRMODE);
    if (!shared) {
        if (rc == -1) {
            FAIL("mkdir failed");
        }
        MPI_Barrier(MPI_COMM_WORLD);
    } else { /* Only one should succeed */
        check_single_success("simul_mkdir", rc, -1);
    }
    end("test");

    begin("cleanup");
    remove_dirs("simul_mkdir", shared);
    end("cleanup");
}

void simul_rmdir(int shared) {
    int rc;
    char *dirname;

    begin("setup");
    dirname = create_dirs("simul_rmdir", shared);
    MPI_Barrier(MPI_COMM_WORLD);
    end("setup");

    begin("test");
    /* All rmdir dirname */
    rc = rmdir(dirname);
    if (!shared) {
        if (rc == -1) {
            FAIL("rmdir failed");
        }
        MPI_Barrier(MPI_COMM_WORLD);
    } else { /* Only one should succeed */
        check_single_success("simul_rmdir", rc, -1);
    }
    end("test");

    begin("cleanup");
    end("cleanup");
}

void simul_creat(int shared) {
    int fd, i;
    char filename[1024];

    begin("setup");
    if (shared)
        sprintf(filename, "%s/simul_creat.0", testdir);
    else
        sprintf(filename, "%s/simul_creat.%d", testdir, rank);
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/simul_creat.%d", testdir, i);
            remove_file_or_dir(buf);
        }
    }
    end("setup");

    begin("test");
    /* All create the files simultaneously */
    fd = creat(filename, FILEMODE);
    if (fd == -1) {
        FAIL("creat failed");
    }
    end("test");

    begin("cleanup");
    /* All close the files one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (close(fd) == -1) {
        FAIL("close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_creat", shared);
    end("cleanup");
}

void simul_unlink(int shared) {
    int rc;
    char *filename;

    begin("setup");
    filename = create_files("simul_unlink", 0, shared);
    end("setup");

    begin("test");
    /* All unlink the files simultaneously */
    rc = unlink(filename);
    if (!shared) {
        if (rc == -1) {
            FAIL("unlink failed");
        }
    } else {
        check_single_success("simul_unlink", rc, -1);
    }
    end("test");

    begin("cleanup");
    end("cleanup");
}

void simul_rename(int shared) {
    int rc, i;
    char *oldfilename;
    char newfilename[1024];
    char *testname = "simul_rename";

    begin("setup");
    oldfilename = create_files(testname, 0, shared);
    sprintf(newfilename, "%s/%s_new.%d", testdir, testname, rank);
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/%s_new.%d", testdir, testname, i);
            remove_file_or_dir(buf);
        }
    }
    end("setup");

    begin("test");
    /* All rename the files simultaneously */
    rc = rename(oldfilename, newfilename);
    if (!shared) {
        if (rc == -1) {
            FAIL("stat failed");
        }
    } else {
        check_single_success(testname, rc, -1);
    }
    end("test");

    begin("cleanup");
    if (rc == 0) {
        if (unlink(newfilename) == -1)
            FAIL("unlink failed");
    }
    end("cleanup");
}

void simul_truncate(int shared) {
    char *filename;

    begin("setup");
    filename = create_files("simul_truncate", 2048, shared);
    end("setup");

    begin("test");
    /* All truncate simultaneously */
    if (truncate(filename, 1024) == -1) {
        FAIL("truncate failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_truncate", shared);
    end("cleanup");
}

void simul_readlink(int shared) {
    char *linkname;
    char buf[1024];

    begin("setup");
    linkname = create_symlinks("simul_readlink", shared);
    end("setup");

    begin("test");
    /* All read the symlink(s) simultaneously */
    if (readlink(linkname, buf, 1024) == -1) {
        FAIL("readlink failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_readlink", shared);
    end("cleanup");
}

void simul_symlink(int shared) {
    int rc, i;
    char linkname[MAX_FILENAME_LEN];
    char filename[MAX_FILENAME_LEN];

    begin("setup");
    if (shared)
        sprintf(linkname, "%s/simul_symlink.0", testdir);
    else
        sprintf(linkname, "%s/simul_symlink.%d", testdir, rank);
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/simul_symlink.%d", testdir, i);
            remove_file_or_dir(buf);
        }
    }
    sprintf(filename, "%s/simul_symlink_target", testdir);
    end("setup");

    begin("test");
    /* All create the symlinks simultaneously */
    rc = symlink(filename, linkname);
    if (!shared) {
        if (rc == -1) {
            FAIL("symlink failed");
        }
    } else {
        check_single_success("simul_symlink", rc, -1);
    }
    end("test");

    begin("cleanup");
    remove_files("simul_symlink", shared);
    end("cleanup");
}

void simul_link_to_one(int shared) {
    int rc, i;
    char *filename;
    char linkname[1024];

    begin("setup");
    if (shared)
        sprintf(linkname, "%s/simul_link.0", testdir);
    else
        sprintf(linkname, "%s/simul_link.%d", testdir, rank);
    if (rank == 0) {
        for (i = 0; i < (shared ? 1 : size); i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/simul_link.%d", testdir, i);
            remove_file_or_dir(buf);
        }
    }
    filename = create_files("simul_link_target", 0, SHARED);
    end("setup");

    begin("test");
    /* All create the hard links simultaneously */
    rc = link(filename, linkname);
    if (!shared) {
        if (rc == -1) {
            FAIL("link failed");
        }
    } else {
        check_single_success("simul_link_to_one", rc, -1);
    }
    end("test");

    begin("cleanup");
    remove_files("simul_link_target", SHARED);
    remove_files("simul_link", shared);
    end("cleanup");
}

void simul_link_to_many(int shared) {
    char *filename;
    char linkname[1024];
    int i;

    if (shared) {
        if (verbose > 0 && rank == 0)
            printf("%s:\tThis is just a place holder; no test is run here.\n",
                    timestamp());
        return;
    }
    begin("setup");
    filename = create_files("simul_link", 0, shared);
    sprintf(linkname, "%s/simul_link_target.%d", testdir, rank);
    if (rank == 0) {
        for (i = 0; i < size; i++) {
            char buf[MAX_FILENAME_LEN];
            sprintf(buf, "%s/simul_link_target.%d", testdir, i);
            remove_file_or_dir(buf);
        }
    }
    end("setup");

    begin("test");
    /* All create the hard links simultaneously */
    if (link(filename, linkname) == -1) {
        FAIL("link failed");
    }
    end("test");

    begin("cleanup");
    remove_files("simul_link", shared);
    remove_files("simul_link_target", !SHARED);
    end("cleanup");
}

void simul_fcntl_lock(int shared) {
    int rc, fd;
    char *filename;
    struct flock sf_lock = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };
    struct flock sf_unlock = {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };

    begin("setup");
    filename = create_files("simul_fcntl", 0, shared);
    MPI_Barrier(MPI_COMM_WORLD);
    /* All open the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if ((fd = open(filename, O_RDWR)) == -1) {
        FAIL("open failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    end("setup");

    begin("test");
    /* All lock the file(s) simultaneously */
    rc = fcntl(fd, F_SETLK, &sf_lock);
    if (!shared) {
        if (rc == -1) {
            if (errno == ENOSYS) {
                if (rank == 0) {
                    fprintf(stdout, "WARNING: fcntl locking not supported.\n");
                    fflush(stdout);
                }
            } else {
                FAIL("fcntl lock failed");
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
    } else {
        int saved_errno = errno;
        int *rc_vec, *er_vec, i;
        int fail = 0;
        int pass = 0;
        int nosys = 0;
        if (rank == 0) {
            if ((rc_vec = (int *)malloc(sizeof(int)*size)) == NULL)
                FAIL("malloc failed");
            if ((er_vec = (int *)malloc(sizeof(int)*size)) == NULL)
                FAIL("malloc failed");
        }
        MPI_Gather(&rc, 1, MPI_INT, rc_vec, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Gather(&saved_errno, 1, MPI_INT, er_vec, 1, MPI_INT, 0,
                MPI_COMM_WORLD);
        if (rank == 0) {
            for (i = 0; i < size; i++) {
                if (rc_vec[i] == -1) {
                    if (er_vec[i] == ENOSYS) {
                        nosys++;
                    } else if (er_vec[i] != EACCES && er_vec[i] != EAGAIN) {
                        errno = er_vec[i];
                        FAIL("fcntl failed as expected, but with wrong errno");
                    }
                    fail++;
                } else {
                    pass++;
                }
            }
            if (nosys == size) {
                fprintf(stdout, "WARNING: fcntl locking not supported.\n");
                fflush(stdout);
            } else if (!((pass == 1) && (fail == size-1))) {
                fprintf(stdout,
                        "%s: FAILED in simul_fcntl_lock", timestamp());
                if (pass > 1)
                    fprintf(stdout,
                            "too many fcntl locks succeeded (%d).\n", pass);
                else
                    fprintf(stdout,
                            "too many fcntl locks failed (%d).\n", fail);
                fflush(stdout);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            free(rc_vec);
            free(er_vec);
        }
    }
    end("test");

    begin("cleanup");
    /* All close the file one at a time */
    Seq_begin(MPI_COMM_WORLD, throttle);
    if (!shared || rank == 0) {
        rc = fcntl(fd, F_SETLK, &sf_unlock);
        if (rc == -1 && errno != ENOSYS)
            FAIL("fcntl unlock failed");
    }
    if (close(fd) == -1) {
        FAIL("close failed");
    }
    Seq_end(MPI_COMM_WORLD, throttle);
    MPI_Barrier(MPI_COMM_WORLD);
    remove_files("simul_fcntl", shared);
    end("cleanup");
}

struct test {
    char *name;
    void (*function) (int);
    int simul; /* Flag designating support for simultaneus mode */
    int indiv; /* Flag designating support for individual mode */
};

static struct test testlist[] = {
    {"open", simul_open},
    {"close", simul_close},
    {"file stat", simul_file_stat},
    {"lseek", simul_lseek},
    {"read", simul_read},
    {"write", simul_write},
    {"chdir", simul_chdir},
    {"directory stat", simul_dir_stat},
    {"statfs", simul_statfs},
    {"readdir", simul_readdir},
    {"mkdir", simul_mkdir},
    {"rmdir", simul_rmdir},
    {"unlink", simul_unlink},
    {"rename", simul_rename},
    {"creat", simul_creat},
    {"truncate", simul_truncate},
    {"symlink", simul_symlink},
    {"readlink", simul_readlink},
    {"link to one file", simul_link_to_one},
    {"link to a file per process", simul_link_to_many},
    {"fcntl locking", simul_fcntl_lock},
    {0}
};

/* Searches an array of ints for one int.  A "-1" must mark the end of the
   array.  */
int int_in_list(int item, int *list) {
    int *ptr;

    if (list == NULL)
        return 0;
    ptr = list;
    while (*ptr != -1) {
        if (*ptr == item)
            return 1;
        ptr += 1;
    }
    return 0;
}

/* Breaks string of comma-sperated ints into an array of ints */
int *string_split(char *string) {
    char *ptr;
    char *tmp;
    int excl_cnt = 1;
    int *list;
    int i;

    ptr = string;
    while((tmp = strchr(ptr, ','))) {
        ptr = tmp + 1;
        excl_cnt++;
    }

    list = (int *)malloc(sizeof(int) * (excl_cnt + 1));
    if (list == NULL) FAIL("malloc failed");

    tmp = (strtok(string, ", "));
    if (tmp == NULL) FAIL("strtok failed");
    list[0] = atoi(tmp);
    for (i = 1; i < excl_cnt; i++) {
        tmp = (strtok(NULL, ", "));
        if (tmp == NULL) FAIL("strtok failed");
        list[i] = atoi(tmp);
    }
    list[i] = -1;

    return list;
}

void print_help(int testcnt) {
    int i;

    if (rank == 0) {
        printf("simul-%s\n", version);
        printf("Usage: simul [-h] -d <testdir> [-f firsttest] [-l lasttest]\n");
        printf("             [-n #] [-N #] [-i \"4,7,13\"] [-e \"6,22\"] [-s]\n");
        printf("             [-v] [-V #]\n");
        printf("\t-h: prints this help message\n");
        printf("\t-d: the directory in which the tests will run\n");
        printf("\t-f: the number of the first test to run (default: 0)\n");
        printf("\t-l: the number of the last test to run (default: %d)\n",
                (testcnt*2)-1);
        printf("\t-i: comma-sperated list of tests to include\n");
        printf("\t-e: comma-sperated list of tests to exclude\n");
        printf("\t-s: single-step through every iteration of every test\n");
        printf("\t-n: repeat each test # times (default: 1)\n");
        printf("\t-N: repeat the entire set of tests # times (default: 1)\n");
        printf("\t-v: increases the verbositly level by 1\n");
        printf("\t-u: test with unifyfs\n");
        printf("\t-V: select a specific verbosity level\n");
        printf("\nThe available tests are:\n");
        for (i = 0; i < testcnt * 2; i++) {
            printf("\tTest #%d: %s, %s mode.\n", i,
                    testlist[i%testcnt].name,
                    (i < testcnt) ? "shared" : "individual");
        }
    }

    MPI_Initialized(&i);
    if (i) MPI_Finalize();
    exit(0);
}

int main(int argc, char **argv) {
    int testcnt;
    int first;
    int last;
    int i, j, k, c;
    int *excl_list = NULL;
    int *incl_list = NULL;
    int test;
    int singlestep = 0;
    int iterations = 1;
    int set_iterations = 1;
    int unifyfs = 0;
    int ret = 0;
    char linebuf[80];

    /* Check for -h parameter before MPI_Init so the simul binary can be
       called directly, without, for instance, mpirun. */
    for (testcnt = 1; testlist[testcnt].name != 0; testcnt++) continue;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help(testcnt);
        }
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        printf("Simul is running with %d process(es)\n", size);
        fflush(stdout);
    }

    first = 0;
    last = testcnt * 2;

    /* Parse command line options */
    while (1) {
        c = getopt(argc, argv, "d:e:f:hi:l:n:N:suvV:");
        if (c == -1)
            break;

        switch (c) {
            case 'd':
                testdir = optarg;
                break;
            case 'e':
                excl_list = string_split(optarg);
                break;
            case 'f':
                first = atoi(optarg);
                if (first >= last) {
                    printf("Invalid parameter, firsttest must be <= lasttest\n");
                    MPI_Abort(MPI_COMM_WORLD, 2);
                }
                break;
            case 'h':
                print_help(testcnt);
                break;
            case 'i':
                incl_list = string_split(optarg);
                break;
            case 'l':
                last = atoi(optarg)+1;
                if (last <= first) {
                    printf("Invalid parameter, lasttest must be >= firsttest\n");
                    MPI_Abort(MPI_COMM_WORLD, 2);
                }
                break;
            case 'n':
                iterations = atoi(optarg);
                break;
            case 'N':
                set_iterations = atoi(optarg);
                break;
            case 's':
                singlestep = 1;
                break;
            case 'u':
                unifyfs = 1;
                break;
            case 'v':
                verbose += 1;
                break;
            case 'V':
                verbose = atoi(optarg);
                break;
        }
    }

    /* mount the unifyfs and use the testdir as the mountpoint.
     * if testdir is not specified, use '/unifyfs.' */
    if (unifyfs) {
        int ret = 0;

        if (!testdir) {
            testdir = "/unifyfs";
        }
        ret = unifyfs_mount(testdir, rank, size);
        if (ret && rank == 0) {
            printf("unifyfs_mount failed (ret=%d)\n", ret);
            MPI_Abort(MPI_COMM_WORLD, 2);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (testdir == NULL && rank == 0) {
        printf("Please specify a test directory! (\"simul -h\" for help)\n");
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    if (gethostname(hostname, 1024) == -1) {
        perror("gethostname");
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    /* If a list of tests was not specified with the -i option, then use
       the first and last number to build a range of included tests. */
    if (incl_list == NULL) {
        incl_list = (int *)malloc(sizeof(int) * (2+last-first));
        for (i = 0; i < last-first; i++) {
            incl_list[i] = i + first;
        }
        incl_list[i] = -1;
    }

    /* Run the tests */
    for (k = 0; k < set_iterations; k++) {
        if ((rank == 0) && (set_iterations > 1))
            printf("%s: Set iteration %d\n", timestamp(), k);
        for (i = 0; ; ++i) {
            test = incl_list[i];
            if (test == -1)
                break;
            if (!int_in_list(test, excl_list)) {
                for (j = 0; j < iterations; j++) {
                    if (singlestep) {
                        if (rank == 0)
                            printf("%s: Hit <Enter> to run test #%d(iter %d): %s, %s mode.\n",
                                    timestamp(), test, j,
                                    testlist[test%testcnt].name,
                                    (test < testcnt) ? "shared" : "individual");
                        fgets(linebuf, 80, stdin);
                    }
                    if (rank == 0) {
                        printf("%s: Running test #%d(iter %d): %s, %s mode.\n",
                                timestamp(), test, j, testlist[test%testcnt].name,
                                (test < testcnt) ? "shared" : "individual");
                        fflush(stdout);
                    }
                    testlist[test%testcnt].function((test < testcnt) ? SHARED : !SHARED);
                    MPI_Barrier(MPI_COMM_WORLD);
                }
            }
        }
    }

    if (rank == 0) printf("%s: All tests passed!\n", timestamp());

    /* unmount unifyfs */
    if (unifyfs) {
        unifyfs_unmount();
    }

    MPI_Finalize();
    exit(0);
}
