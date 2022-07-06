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

#include "unifyfs-internal.h"
#include "unifyfs-stdio.h"
#include "unifyfs-sysio.h"
#include <gotcha/gotcha.h>

/* define gotcha-specific state to use with our wrapper */
#define UNIFYFS_DEF(name, ret, args, argnames) \
gotcha_wrappee_handle_t wrappee_handle_ ## name; \
ret (*__real_ ## name) args = NULL;

UNIFYFS_DEF(access, int,
            (const char* path, int mode),
            (path, mode))
UNIFYFS_DEF(chmod, int,
            (const char* path, mode_t mode),
            (path, mode))
UNIFYFS_DEF(mkdir, int,
            (const char* path, mode_t mode),
            (path, mode))
UNIFYFS_DEF(rmdir, int,
            (const char* path),
            (path))
UNIFYFS_DEF(chdir, int,
            (const char* path),
            (path))
UNIFYFS_DEF(__getcwd_chk, char*,
            (char* path, size_t size, size_t buflen),
            (path, size, buflen))
UNIFYFS_DEF(getcwd, char*,
            (char* path, size_t size),
            (path, size))
UNIFYFS_DEF(getwd, char*,
            (char* path),
            (path))
UNIFYFS_DEF(get_current_dir_name, char*,
            (void),
            ())
UNIFYFS_DEF(rename, int,
            (const char* oldpath, const char* newpath),
            (oldpath, newpath))
UNIFYFS_DEF(truncate, int,
            (const char* path, off_t length),
            (path, length))
UNIFYFS_DEF(unlink, int,
            (const char* path),
            (path))
UNIFYFS_DEF(remove, int,
            (const char* path),
            (path))

UNIFYFS_DEF(stat, int,
            (const char* path, struct stat* buf),
            (path, buf))
UNIFYFS_DEF(fstat, int,
            (int fd, struct stat* buf),
            (fd, buf))
UNIFYFS_DEF(__xstat, int,
            (int vers, const char* path, struct stat* buf),
            (vers, path, buf))
UNIFYFS_DEF(__fxstat, int,
            (int vers, int fd, struct stat* buf),
            (vers, fd, buf))
UNIFYFS_DEF(__lxstat, int,
            (int vers, const char* path, struct stat* buf),
            (vers, path, buf))
UNIFYFS_DEF(statfs, int,
            (const char* path, struct statfs* fsbuf),
            (path, fsbuf))
UNIFYFS_DEF(fstatfs, int,
            (int fd, struct statfs* fsbuf),
            (fd, fsbuf))

UNIFYFS_DEF(creat, int,
            (const char* path, mode_t mode),
            (path, mode))
UNIFYFS_DEF(creat64, int,
            (const char* path, mode_t mode),
            (path, mode))
UNIFYFS_DEF(open, int,
            (const char* path, int flags, ...),
            (path, flags))
UNIFYFS_DEF(open64, int,
            (const char* path, int flags, ...),
            (path, flags))
UNIFYFS_DEF(__open_2, int,
            (const char* path, int flags, ...),
            (path, flags))

#ifdef HAVE_LIO_LISTIO
UNIFYFS_DEF(lio_listio, int,
            (int m, struct aiocb* const cblist[], int n, struct sigevent* sep),
            (m, cblist, n, sep))
#endif

UNIFYFS_DEF(lseek, off_t,
            (int fd, off_t offset, int whence),
            (fd, offset, whence))
UNIFYFS_DEF(lseek64, off64_t,
            (int fd, off64_t offset, int whence),
            (fd, offset, whence))

UNIFYFS_DEF(posix_fadvise, int,
            (int fd, off_t offset, off_t len, int advice),
            (fd, offset, len, advice))

UNIFYFS_DEF(read, ssize_t,
            (int fd, void* buf, size_t count),
            (fd, buf, count))
UNIFYFS_DEF(write, ssize_t,
            (int fd, const void* buf, size_t count),
            (fd, buf, count))
UNIFYFS_DEF(readv, ssize_t,
            (int fd, const struct iovec* iov, int iovcnt),
            (fd, iov, iovcnt))
UNIFYFS_DEF(writev, ssize_t,
            (int fd, const struct iovec* iov, int iovcnt),
            (fd, iov, iovcnt))
UNIFYFS_DEF(pread, ssize_t,
            (int fd, void* buf, size_t count, off_t off),
            (fd, buf, count, off))
UNIFYFS_DEF(pread64, ssize_t,
            (int fd, void* buf, size_t count, off64_t off),
            (fd, buf, count, off))
UNIFYFS_DEF(pwrite, ssize_t,
            (int fd, const void* buf, size_t count, off_t off),
            (fd, buf, count, off))
UNIFYFS_DEF(pwrite64, ssize_t,
            (int fd, const void* buf, size_t count, off64_t off),
            (fd, buf, count, off))
UNIFYFS_DEF(close, int,
            (int fd),
            (fd))
UNIFYFS_DEF(dup, int,
            (int fd),
            (fd))
UNIFYFS_DEF(dup2, int,
            (int fd, int desired_fd),
            (fd, desired_fd))
UNIFYFS_DEF(fchdir, int,
            (int fd),
            (fd))
UNIFYFS_DEF(ftruncate, int,
            (int fd, off_t length),
            (fd, length))
UNIFYFS_DEF(fsync, int,
            (int fd),
            (fd))
UNIFYFS_DEF(fdatasync, int,
            (int fd),
            (fd))
UNIFYFS_DEF(flock, int,
            (int fd, int operation),
            (fd, operation))
UNIFYFS_DEF(fchmod, int,
            (int fd, mode_t mode),
            (fd, mode))

UNIFYFS_DEF(mmap, void*,
            (void* addr, size_t len, int prot, int fl, int fd, off_t off),
            (addr, len, prot, fl, fd, off))
UNIFYFS_DEF(msync, int,
            (void* addr, size_t length, int flags),
            (addr, length, flags))
UNIFYFS_DEF(munmap, int,
            (void* addr, size_t length),
            (addr, length))
UNIFYFS_DEF(mmap64, void*,
            (void* addr, size_t len, int prot, int fl, int fd, off_t off),
            (addr, len, prot, fl, fd, off))

UNIFYFS_DEF(opendir, DIR*,
            (const char* name),
            (name))
UNIFYFS_DEF(fdopendir, DIR*,
            (int fd),
            (fd))
UNIFYFS_DEF(closedir, int,
            (DIR* dirp),
            (dirp))
UNIFYFS_DEF(readdir, struct dirent*,
            (DIR* dirp),
            (dirp))
UNIFYFS_DEF(rewinddir, void,
            (DIR* dirp),
            (dirp))
UNIFYFS_DEF(dirfd, int,
            (DIR* dirp),
            (dirp))
UNIFYFS_DEF(telldir, long,
            (DIR* dirp),
            (dirp))
UNIFYFS_DEF(scandir, int,
            (const char* dirp, struct dirent** namelist,
             int (*filter)(const struct dirent*),
             int (*compar)(const struct dirent**, const struct dirent**)),
            (dirp, namelist, filter, compar))
UNIFYFS_DEF(seekdir, void,
            (DIR* dirp, long loc),
            (dirp, loc))

UNIFYFS_DEF(fopen, FILE*,
            (const char* path, const char* mode),
            (path, mode))
UNIFYFS_DEF(freopen, FILE*,
            (const char* path, const char* mode, FILE* stream),
            (path, mode, stream))
UNIFYFS_DEF(setvbuf, int,
            (FILE* stream, char* buf, int type, size_t size),
            (stream, buf, type, size))
UNIFYFS_DEF(setbuf, void,
            (FILE* stream, char* buf),
            (stream, buf))
UNIFYFS_DEF(ungetc, int,
            (int c, FILE* stream),
            (c, stream))
UNIFYFS_DEF(fgetc, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fputc, int,
            (int c, FILE* stream),
            (c, stream))
UNIFYFS_DEF(getc, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(putc, int,
            (int c, FILE* stream),
            (c, stream))
UNIFYFS_DEF(fgets, char*,
            (char* s, int n, FILE* stream),
            (s, n, stream))
UNIFYFS_DEF(fputs, int,
            (const char* s, FILE* stream),
            (s, stream))
UNIFYFS_DEF(fread, size_t,
            (void* ptr, size_t size, size_t nitems, FILE* stream),
            (ptr, size, nitems, stream))
UNIFYFS_DEF(fwrite, size_t,
            (const void* ptr, size_t size, size_t nitems, FILE* stream),
            (ptr, size, nitems, stream))
UNIFYFS_DEF(fprintf, int,
            (FILE* stream, const char* format, ...),
            (stream, format))
UNIFYFS_DEF(fscanf, int,
            (FILE* stream, const char* format, ...),
            (stream, format))
UNIFYFS_DEF(vfprintf, int,
            (FILE* stream, const char* format, va_list args),
            (stream, format, args))
UNIFYFS_DEF(vfscanf, int,
            (FILE* stream, const char* format, va_list args),
            (stream, format, args))
UNIFYFS_DEF(fseek, int,
            (FILE* stream, long offset, int whence),
            (stream, offset, whence))
UNIFYFS_DEF(fseeko, int,
            (FILE* stream, off_t offset, int whence),
            (stream, offset, whence))
UNIFYFS_DEF(ftell, long,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(ftello, off_t,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(rewind, void,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fgetpos, int,
            (FILE* stream, fpos_t* pos),
            (stream, pos))
UNIFYFS_DEF(fsetpos, int,
            (FILE* stream, const fpos_t* pos),
            (stream, pos))
UNIFYFS_DEF(fflush, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(feof, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(ferror, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(clearerr, void,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fileno, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fclose, int,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fwprintf, int,
            (FILE* stream, const wchar_t* format, ...),
            (stream, format))
UNIFYFS_DEF(fwscanf, int,
            (FILE* stream, const wchar_t* format, ...),
            (stream, format))
UNIFYFS_DEF(vfwprintf, int,
            (FILE* stream, const wchar_t* format, va_list args),
            (stream, format, args))
UNIFYFS_DEF(vfwscanf, int,
            (FILE* stream, const wchar_t* format, va_list args),
            (stream, format, args))
UNIFYFS_DEF(fgetwc, wint_t,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(fgetws, wchar_t*,
            (wchar_t* s, int n, FILE* stream),
            (s, n, stream))
UNIFYFS_DEF(fputwc, wint_t,
            (wchar_t wc, FILE* stream),
            (wc, stream))
UNIFYFS_DEF(fputws, int,
            (const wchar_t* s, FILE* stream),
            (s, stream))
UNIFYFS_DEF(fwide, int,
            (FILE* stream, int mode),
            (stream, mode))
UNIFYFS_DEF(getwc, wint_t,
            (FILE* stream),
            (stream))
UNIFYFS_DEF(putwc, wint_t,
            (wchar_t c, FILE* stream),
            (c, stream))
UNIFYFS_DEF(ungetwc, wint_t,
            (wint_t c, FILE* stream),
            (c, stream))

struct gotcha_binding_t unifyfs_wrappers[] = {
    { "access", UNIFYFS_WRAP(access), &wrappee_handle_access },
    { "chmod", UNIFYFS_WRAP(chmod), &wrappee_handle_chmod },
    { "mkdir", UNIFYFS_WRAP(mkdir), &wrappee_handle_mkdir },
    { "rmdir", UNIFYFS_WRAP(rmdir), &wrappee_handle_rmdir },
    { "chdir", UNIFYFS_WRAP(chdir), &wrappee_handle_chdir },
    { "__getcwd_chk", UNIFYFS_WRAP(__getcwd_chk),
        &wrappee_handle___getcwd_chk },
    { "getcwd", UNIFYFS_WRAP(getcwd), &wrappee_handle_getcwd },
    { "getwd", UNIFYFS_WRAP(getwd), &wrappee_handle_getwd },
    { "get_current_dir_name", UNIFYFS_WRAP(get_current_dir_name),
        &wrappee_handle_get_current_dir_name },
    { "rename", UNIFYFS_WRAP(rename), &wrappee_handle_rename },
    { "truncate", UNIFYFS_WRAP(truncate), &wrappee_handle_truncate },
    { "unlink", UNIFYFS_WRAP(unlink), &wrappee_handle_unlink },
    { "remove", UNIFYFS_WRAP(remove), &wrappee_handle_remove },
    { "stat", UNIFYFS_WRAP(stat), &wrappee_handle_stat },
    { "fstat", UNIFYFS_WRAP(fstat), &wrappee_handle_fstat },
    { "__xstat", UNIFYFS_WRAP(__xstat), &wrappee_handle___xstat },
    { "__fxstat", UNIFYFS_WRAP(__fxstat), &wrappee_handle___fxstat },
    { "__lxstat", UNIFYFS_WRAP(__lxstat), &wrappee_handle___lxstat },
    { "statfs", UNIFYFS_WRAP(statfs), &wrappee_handle_statfs },
    { "fstatfs", UNIFYFS_WRAP(fstatfs), &wrappee_handle_fstatfs },
    { "creat", UNIFYFS_WRAP(creat), &wrappee_handle_creat },
    { "creat64", UNIFYFS_WRAP(creat64), &wrappee_handle_creat64 },
    { "open", UNIFYFS_WRAP(open), &wrappee_handle_open },
    { "open64", UNIFYFS_WRAP(open64), &wrappee_handle_open64 },
    { "__open_2", UNIFYFS_WRAP(__open_2), &wrappee_handle___open_2 },
#ifdef HAVE_LIO_LISTIO
    { "lio_listio", UNIFYFS_WRAP(lio_listio), &wrappee_handle_lio_listio },
#endif
    { "lseek", UNIFYFS_WRAP(lseek), &wrappee_handle_lseek },
    { "lseek64", UNIFYFS_WRAP(lseek64), &wrappee_handle_lseek64 },
    { "posix_fadvise", UNIFYFS_WRAP(posix_fadvise), &wrappee_handle_posix_fadvise },
    { "read", UNIFYFS_WRAP(read), &wrappee_handle_read },
    { "write", UNIFYFS_WRAP(write), &wrappee_handle_write },
    { "readv", UNIFYFS_WRAP(readv), &wrappee_handle_readv },
    { "writev", UNIFYFS_WRAP(writev), &wrappee_handle_writev },
    { "pread", UNIFYFS_WRAP(pread), &wrappee_handle_pread },
    { "pread64", UNIFYFS_WRAP(pread64), &wrappee_handle_pread64 },
    { "pwrite", UNIFYFS_WRAP(pwrite), &wrappee_handle_pwrite },
    { "pwrite64", UNIFYFS_WRAP(pwrite64), &wrappee_handle_pwrite64 },
    { "fchdir", UNIFYFS_WRAP(fchdir), &wrappee_handle_fchdir },
    { "ftruncate", UNIFYFS_WRAP(ftruncate), &wrappee_handle_ftruncate },
    { "fsync", UNIFYFS_WRAP(fsync), &wrappee_handle_fsync },
    { "fdatasync", UNIFYFS_WRAP(fdatasync), &wrappee_handle_fdatasync },
    { "flock", UNIFYFS_WRAP(flock), &wrappee_handle_flock },
    { "fchmod", UNIFYFS_WRAP(fchmod), &wrappee_handle_fchmod },
    { "mmap", UNIFYFS_WRAP(mmap), &wrappee_handle_mmap },
    { "msync", UNIFYFS_WRAP(msync), &wrappee_handle_msync },
    { "munmap", UNIFYFS_WRAP(munmap), &wrappee_handle_munmap },
    { "mmap64", UNIFYFS_WRAP(mmap64), &wrappee_handle_mmap64 },
    { "close", UNIFYFS_WRAP(close), &wrappee_handle_close },
    { "dup", UNIFYFS_WRAP(dup), &wrappee_handle_dup },
    { "dup2", UNIFYFS_WRAP(dup2), &wrappee_handle_dup2 },
    { "opendir", UNIFYFS_WRAP(opendir), &wrappee_handle_opendir },
    { "fdopendir", UNIFYFS_WRAP(fdopendir), &wrappee_handle_fdopendir },
    { "closedir", UNIFYFS_WRAP(closedir), &wrappee_handle_closedir },
    { "readdir", UNIFYFS_WRAP(readdir), &wrappee_handle_readdir },
    { "rewinddir", UNIFYFS_WRAP(rewinddir), &wrappee_handle_rewinddir },
    { "dirfd", UNIFYFS_WRAP(dirfd), &wrappee_handle_dirfd },
    { "telldir", UNIFYFS_WRAP(telldir), &wrappee_handle_telldir },
    { "scandir", UNIFYFS_WRAP(scandir), &wrappee_handle_scandir },
    { "seekdir", UNIFYFS_WRAP(seekdir), &wrappee_handle_seekdir },
    { "fopen", UNIFYFS_WRAP(fopen), &wrappee_handle_fopen },
    { "freopen", UNIFYFS_WRAP(freopen), &wrappee_handle_freopen },
    { "setvbuf", UNIFYFS_WRAP(setvbuf), &wrappee_handle_setvbuf },
    { "setbuf", UNIFYFS_WRAP(setbuf), &wrappee_handle_setbuf },
    { "ungetc", UNIFYFS_WRAP(ungetc), &wrappee_handle_ungetc },
    { "fgetc", UNIFYFS_WRAP(fgetc), &wrappee_handle_fgetc },
    { "fputc", UNIFYFS_WRAP(fputc), &wrappee_handle_fputc },
    { "getc", UNIFYFS_WRAP(getc), &wrappee_handle_getc },
    { "putc", UNIFYFS_WRAP(putc), &wrappee_handle_putc },
    { "fgets", UNIFYFS_WRAP(fgets), &wrappee_handle_fgets },
    { "fputs", UNIFYFS_WRAP(fputs), &wrappee_handle_fputs },
    { "fread", UNIFYFS_WRAP(fread), &wrappee_handle_fread },
    { "fwrite", UNIFYFS_WRAP(fwrite), &wrappee_handle_fwrite },
    { "fprintf", UNIFYFS_WRAP(fprintf), &wrappee_handle_fprintf },
    { "vfprintf", UNIFYFS_WRAP(vfprintf), &wrappee_handle_vfprintf },
    { "fscanf", UNIFYFS_WRAP(fscanf), &wrappee_handle_fscanf },
    { "vfscanf", UNIFYFS_WRAP(vfscanf), &wrappee_handle_vfscanf },
    { "fseek", UNIFYFS_WRAP(fseek), &wrappee_handle_fseek },
    { "fseeko", UNIFYFS_WRAP(fseeko), &wrappee_handle_fseeko },
    { "ftell", UNIFYFS_WRAP(ftell), &wrappee_handle_ftell },
    { "ftello", UNIFYFS_WRAP(ftello), &wrappee_handle_ftello },
    { "rewind", UNIFYFS_WRAP(rewind), &wrappee_handle_rewind },
    { "fgetpos", UNIFYFS_WRAP(fgetpos), &wrappee_handle_fgetpos },
    { "fsetpos", UNIFYFS_WRAP(fsetpos), &wrappee_handle_fsetpos },
    { "fflush", UNIFYFS_WRAP(fflush), &wrappee_handle_fflush },
    { "feof", UNIFYFS_WRAP(feof), &wrappee_handle_feof },
    { "ferror", UNIFYFS_WRAP(ferror), &wrappee_handle_ferror },
    { "clearerr", UNIFYFS_WRAP(clearerr), &wrappee_handle_clearerr },
    { "fileno", UNIFYFS_WRAP(fileno), &wrappee_handle_fileno },
    { "fclose", UNIFYFS_WRAP(fclose), &wrappee_handle_fclose },
    { "fwprintf", UNIFYFS_WRAP(fwprintf), &wrappee_handle_fwprintf },
    { "fwscanf", UNIFYFS_WRAP(fwscanf), &wrappee_handle_fwscanf },
    { "vfwprintf", UNIFYFS_WRAP(vfwprintf), &wrappee_handle_vfwprintf },
    { "vfwscanf", UNIFYFS_WRAP(vfwscanf), &wrappee_handle_vfwscanf },
    { "fgetwc", UNIFYFS_WRAP(fgetwc), &wrappee_handle_fgetwc },
    { "fgetws", UNIFYFS_WRAP(fgetws), &wrappee_handle_fgetws },
    { "fputwc", UNIFYFS_WRAP(fputwc), &wrappee_handle_fputwc },
    { "fputws", UNIFYFS_WRAP(fputws), &wrappee_handle_fputws },
    { "fwide", UNIFYFS_WRAP(fwide), &wrappee_handle_fwide },
    { "getwc", UNIFYFS_WRAP(getwc), &wrappee_handle_getwc },
    { "putwc", UNIFYFS_WRAP(putwc), &wrappee_handle_putwc },
    { "ungetwc", UNIFYFS_WRAP(ungetwc), &wrappee_handle_ungetwc },
};

#define GOTCHA_NFUNCS (sizeof(unifyfs_wrappers) / sizeof(gotcha_binding_t))

int setup_gotcha_wrappers(void)
{
    /* insert our I/O wrappers using gotcha */
    enum gotcha_error_t result;
    result = gotcha_wrap(unifyfs_wrappers, GOTCHA_NFUNCS, "unifyfs");
    if (result != GOTCHA_SUCCESS) {
        LOGERR("gotcha_wrap() returned %d", (int) result);
        if (result == GOTCHA_FUNCTION_NOT_FOUND) {
            /* one or more functions were not found */
            void* fn;
            gotcha_wrappee_handle_t* hdlptr;
            for (int i = 0; i < GOTCHA_NFUNCS; i++) {
                hdlptr = unifyfs_wrappers[i].function_handle;
                fn = gotcha_get_wrappee(*hdlptr);
                if (NULL == fn) {
                    LOGWARN("Gotcha failed to wrap function '%s'",
                            unifyfs_wrappers[i].name);
                }
            }
        } else {
            return UNIFYFS_ERROR_GOTCHA;
        }
    }
    return UNIFYFS_SUCCESS;
}
