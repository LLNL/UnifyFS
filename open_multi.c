#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "mpi.h"
#include "unifyfs.h"

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

    int ret = unifyfs_mount(mountpoint, rank, ranks, 0);

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
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL);
    if (fd >= 0) {
        success = 1;
        close(fd);
    } else if (errno == EEXIST) {
        eexist = 1;
    }

    /* one rank should win */
    int sum = mpi_sum(success);
    if (sum != 1) {
    }

    /* all others should get EEXIST */
    sum = mpi_sum(eexist);
    if (sum != (ranks - 1)) {
    }

    MPI_Barrier(MPI_COMM_WORLD);

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
    if (sum != 1) {
    }

    /* everyone else should get ENOENT */
    sum = mpi_sum(enoent);
    if (sum != (ranks - 1)) {
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /* all create, this time not exclusive */
    errno = 0;
    success = 0;
    fd = open(file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        success = 1;
        close(fd);
    }

    /* all should succeed */
    sum = mpi_sum(success);
    if (sum != ranks) {
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /* open file for writing */
    errno = 0;
    success = 0;
    fd = open(file, O_WRONLY);
    if (fd >= 0) {
        success = 1;
        close(fd);
    }

    /* all should succeeed */
    sum = mpi_sum(success);
    if (sum != ranks) {
    }

    MPI_Barrier(MPI_COMM_WORLD);

    unlink(file);

    MPI_Barrier(MPI_COMM_WORLD);

    /* have all ranks write to a different section of the file,
     * then open file with truncate on one rank to verify size change */
    fd = open(file, O_WRONLY | O_CREAT);
    if (fd >= 0) {
        off_t offset = (off_t) (rank * bufsize);
        pwrite(fd, buf, bufsize, offset);
        fsync(fd);
        close(fd);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        /* all ranks should have written some data */
        off_t size = getsize(file);
        if (size != bufsize * ranks) {
        }

        /* create file with truncate */
        fd = open(file, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            close(fd);
        }

        /* now file should be 0 length again */
        size = getsize(file);
        if (size != 0) {
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    unlink(file);

    MPI_Barrier(MPI_COMM_WORLD);

    free(buf);

    unifyfs_unmount();

    MPI_Finalize();

    return 0;
}
