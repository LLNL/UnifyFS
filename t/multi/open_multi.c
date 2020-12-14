#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "t/lib/tap.h"
#include "t/lib/testutil.h"

#include "mpi.h"
#include "unifyfs.h"

#define all_ok(condition, comm, ...) \
    do { \
        int input = (int) condition; \
        int output; \
        MPI_Allreduce(&input, &output, 1, MPI_INT, MPI_LAND, comm); \
        int rank; \
        MPI_Comm_rank(comm, &rank); \
        if (rank == 0) { \
            ok_at_loc(__FILE__, __LINE__, output, __VA_ARGS__, NULL); \
        } \
    } while (0)

int mpi_sum(int val)
{
    int sum;
    MPI_Allreduce(&val, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    return sum;
}

off_t getsize(char* file)
{
    off_t size = (off_t)-1;
    struct stat buf;
    int rc = stat(file, &buf);
    if (rc == 0) {
        size = buf.st_size;
    }
    return size;
}

int main(int argc, char* argv[])
{
    char mountpoint[] = "/unifyfs";

    MPI_Init(&argc, &argv);

    int rank, ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);

    unifyfs_mount(mountpoint, rank, ranks, 0);

    char file[256];
    sprintf(file, "%s/testfile", mountpoint);

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);
    memset(buf, rank, bufsize);

    MPI_Barrier(MPI_COMM_WORLD);

    /* create a file in exclusive mode,
     * one rank should win, all others should get EEXIST */
    errno = 0;
    int success = 0;
    int eexist  = 0;
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0700);
    if (fd >= 0) {
        success = 1;
        close(fd);
    } else if (errno == EEXIST) {
        eexist = 1;
    }

    /* one rank should win */
    int sum = mpi_sum(success);
    all_ok(sum == 1, MPI_COMM_WORLD,
        "More than one process opened file in exclusive mode");

    /* all others should get EEXIST */
    sum = mpi_sum(eexist);
    all_ok(sum == (ranks - 1), MPI_COMM_WORLD,
        "All but one process should get EEXIST when opening file in exclusive mode");

    /* all delete,
     * one rank should win, all others should get ENOENT */
    errno = 0;
    success = 0;
    int enoent = 0;
    int rc = unlink(file);
    if (rc == 0) {
        success = 1;
    } else if (errno == ENOENT) {
        enoent = 1;
    }

    /* one winner */
    sum = mpi_sum(success);
    all_ok(sum == 1, MPI_COMM_WORLD,
        "More than one process got success on unlink of the same file");

    /* everyone else should get ENOENT */
    sum = mpi_sum(enoent);
    all_ok(sum == (ranks - 1), MPI_COMM_WORLD,
        "All but one process should get ENOENT when unlinking the same file");

    /* all create, this time not exclusive */
    errno = 0;
    success = 0;
    fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd >= 0) {
        success = 1;
        close(fd);
    }

    /* all should succeed */
    all_ok(success, MPI_COMM_WORLD,
        "All processes should open file with O_CREAT and not O_EXCL");

    /* open file for writing */
    errno = 0;
    success = 0;
    fd = open(file, O_WRONLY);
    if (fd >= 0) {
        success = 1;
        close(fd);
    }

    /* all should succeeed */
    all_ok(success, MPI_COMM_WORLD,
        "All processes should open file with O_CREAT for writing");

    MPI_Barrier(MPI_COMM_WORLD);

    unlink(file);

    MPI_Barrier(MPI_COMM_WORLD);

    /* have all ranks write to a different section of the file,
     * then open file with truncate on one rank to verify size change */
    success = 0;
    fd = open(file, O_WRONLY | O_CREAT, 0700);
    if (fd >= 0) {
        success = 1;
        off_t offset = (off_t) (rank * bufsize);
        ssize_t nwritten = pwrite(fd, buf, bufsize, offset);
        if (nwritten != bufsize) {
            success = 0;
        }
        fsync(fd);
        close(fd);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        /* all ranks should have written some data */
        off_t size = getsize(file);
        ok(size == bufsize * ranks,
            "File size %lu does not match expected size %lu",
            size, bufsize * ranks);

        /* create file with truncate */
        fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0700);
        if (fd >= 0) {
            close(fd);
        }

        /* now file should be 0 length again */
        size = getsize(file);
        ok(size == 0,
            "File size %lu does not match expected size %lu",
            size, 0);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    unlink(file);

    MPI_Barrier(MPI_COMM_WORLD);

    free(buf);

    unifyfs_unmount();

    MPI_Finalize();

    return 0;
}
