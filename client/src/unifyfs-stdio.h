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
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of UnifyFS.
 * For details, see https://github.com/llnl/unifyfs
 * Please read https://github.com/llnl/unifyfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of UNIFYFS.
 * For details, see https://github.com/hpc/unifyfs
 * Please also read this file LICENSE.UNIFYFS
 */

#ifndef UNIFYFS_STDIO_H
#define UNIFYFS_STDIO_H

#include "unifyfs-internal.h"
#include "unifyfs_wrap.h"

/* TODO: declare the wrapper functions we define in unifyfs-stdio.c
 * so other routines can call them */
UNIFYFS_DECL(fclose, int, (FILE* stream));
UNIFYFS_DECL(fflush, int, (FILE* stream));
UNIFYFS_DECL(fopen, FILE*, (const char* path, const char* mode));
UNIFYFS_DECL(fopen64, FILE*, (const char* path, const char* mode));
UNIFYFS_DECL(freopen, FILE*, (const char* path, const char* mode,
                              FILE* stream));
UNIFYFS_DECL(setbuf, void, (FILE* stream, char* buf));
UNIFYFS_DECL(setvbuf, int, (FILE* stream, char* buf, int type, size_t size));

UNIFYFS_DECL(fprintf,  int, (FILE* stream, const char* format, ...));
UNIFYFS_DECL(fscanf,   int, (FILE* stream, const char* format, ...));
UNIFYFS_DECL(vfprintf, int, (FILE* stream, const char* format, va_list ap));
UNIFYFS_DECL(vfscanf,  int, (FILE* stream, const char* format, va_list ap));

UNIFYFS_DECL(fgetc,  int, (FILE* stream));
UNIFYFS_DECL(fgets,  char*, (char* s, int n, FILE* stream));
UNIFYFS_DECL(fputc,  int, (int c, FILE* stream));
UNIFYFS_DECL(fputs,  int, (const char* s, FILE* stream));
UNIFYFS_DECL(getc,   int, (FILE* stream));
UNIFYFS_DECL(putc,   int, (int c, FILE* stream));
UNIFYFS_DECL(ungetc, int, (int c, FILE* stream));

UNIFYFS_DECL(fread,  size_t, (void* ptr, size_t size, size_t nitems,
                              FILE* stream));
UNIFYFS_DECL(fwrite, size_t, (const void* ptr, size_t size, size_t nitems,
                              FILE* stream));

UNIFYFS_DECL(fgetpos, int, (FILE* stream, fpos_t* pos));
UNIFYFS_DECL(fseek,   int, (FILE* stream, long offset,  int whence));
UNIFYFS_DECL(fsetpos, int, (FILE* stream, const fpos_t* pos));
UNIFYFS_DECL(ftell,   long, (FILE* stream));
UNIFYFS_DECL(rewind,  void, (FILE* stream));

UNIFYFS_DECL(clearerr, void, (FILE* stream));
UNIFYFS_DECL(feof,     int, (FILE* stream));
UNIFYFS_DECL(ferror,   int, (FILE* stream));

UNIFYFS_DECL(fseeko, int, (FILE* stream, off_t offset, int whence));
UNIFYFS_DECL(ftello, off_t, (FILE* stream));
UNIFYFS_DECL(fileno, int, (FILE* stream));

/* http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf 7.24 */

UNIFYFS_DECL(fwprintf,  int, (FILE* stream, const wchar_t* format, ...));
UNIFYFS_DECL(fwscanf,   int, (FILE* stream, const wchar_t* format, ...));
UNIFYFS_DECL(vfwprintf, int, (FILE* stream, const wchar_t* format,
                              va_list arg));
UNIFYFS_DECL(vfwscanf,  int, (FILE* stream, const wchar_t* format,
                              va_list arg));
UNIFYFS_DECL(fgetwc,    wint_t, (FILE* stream));
UNIFYFS_DECL(fgetws,    wchar_t*, (wchar_t* s, int n, FILE* stream));
UNIFYFS_DECL(fputwc,    wint_t, (wchar_t wc, FILE* stream));
UNIFYFS_DECL(fputws,    int, (const wchar_t* s, FILE* stream));
UNIFYFS_DECL(fwide,     int, (FILE* stream, int mode));
UNIFYFS_DECL(getwc,     wint_t, (FILE* stream));
UNIFYFS_DECL(putwc,     wint_t, (wchar_t c, FILE* stream));
UNIFYFS_DECL(ungetwc,   wint_t, (wint_t c, FILE* stream));

#endif /* UNIFYFS_STDIO_H */
