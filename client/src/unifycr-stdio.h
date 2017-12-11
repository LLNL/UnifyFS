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
 * This file is part of UnifyCR.
 * For details, see https://github.com/llnl/unifycr
 * Please read https://github.com/llnl/unifycr/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of UNIFYCR.
 * For details, see https://github.com/hpc/unifycr
 * Please also read this file LICENSE.UNIFYCR
 */

#ifndef UNIFYCR_STDIO_H
#define UNIFYCR_STDIO_H
#include <wchar.h>

#include "unifycr-internal.h"

/* TODO: declare the wrapper functions we define in unifycr-stdio.c
 * so other routines can call them */
UNIFYCR_DECL(fclose, int, (FILE *stream));
UNIFYCR_DECL(fflush, int, (FILE *stream));
UNIFYCR_DECL(fopen, FILE*, (const char *path, const char *mode));
UNIFYCR_DECL(freopen, FILE*, (const char *path, const char *mode, FILE *stream));
UNIFYCR_DECL(setbuf, void*, (FILE *stream, char* buf));
UNIFYCR_DECL(setvbuf, int, (FILE *stream, char* buf, int type, size_t size));

UNIFYCR_DECL(fprintf,  int, (FILE* stream, const char* format, ...));
UNIFYCR_DECL(fscanf,   int, (FILE* stream, const char* format, ...));
UNIFYCR_DECL(vfprintf, int, (FILE* stream, const char* format, va_list ap));
UNIFYCR_DECL(vfscanf,  int, (FILE* stream, const char* format, va_list ap));

UNIFYCR_DECL(fgetc,  int,   (FILE *stream));
UNIFYCR_DECL(fgets,  char*, (char* s, int n, FILE* stream));
UNIFYCR_DECL(fputc,  int,   (int c, FILE *stream));
UNIFYCR_DECL(fputs,  int,   (const char* s, FILE* stream));
UNIFYCR_DECL(getc,   int,   (FILE *stream));
UNIFYCR_DECL(putc,   int,   (int c, FILE *stream));
UNIFYCR_DECL(ungetc, int,   (int c, FILE *stream));

UNIFYCR_DECL(fread,  size_t, (void *ptr, size_t size, size_t nitems, FILE *stream));
UNIFYCR_DECL(fwrite, size_t, (const void *ptr, size_t size, size_t nitems, FILE *stream));

UNIFYCR_DECL(fgetpos, int,  (FILE *stream, fpos_t* pos));
UNIFYCR_DECL(fseek,   int,  (FILE *stream, long offset,  int whence));
UNIFYCR_DECL(fsetpos, int,  (FILE *stream, const fpos_t* pos));
UNIFYCR_DECL(ftell,   long, (FILE *stream));
UNIFYCR_DECL(rewind,  void, (FILE *stream));

UNIFYCR_DECL(clearerr, void, (FILE *stream));
UNIFYCR_DECL(feof,     int,  (FILE *stream));
UNIFYCR_DECL(ferror,   int,  (FILE *stream));

UNIFYCR_DECL(fseeko, int,   (FILE *stream, off_t offset, int whence));
UNIFYCR_DECL(ftello, off_t, (FILE *stream));
UNIFYCR_DECL(fileno, int,   (FILE *stream));

/* http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf 7.24 */

UNIFYCR_DECL(fwprintf,  int,      (FILE *stream, const wchar_t* format, ...));
UNIFYCR_DECL(fwscanf,   int,      (FILE *stream, const wchar_t* format, ...));
UNIFYCR_DECL(vfwprintf, int,      (FILE *stream, const wchar_t* format, va_list arg));
UNIFYCR_DECL(vfwscanf,  int,      (FILE *stream, const wchar_t* format, va_list arg));
UNIFYCR_DECL(fgetwc,    wint_t,   (FILE *stream));
UNIFYCR_DECL(fgetws,    wchar_t*, (wchar_t* s, int n, FILE *stream));
UNIFYCR_DECL(fputwc,    wint_t,   (wchar_t wc, FILE *stream));
UNIFYCR_DECL(fputws,    int,      (const wchar_t* s, FILE *stream));
UNIFYCR_DECL(fwide,     int,      (FILE *stream, int mode));
UNIFYCR_DECL(getwc,     wint_t,   (FILE *stream));
UNIFYCR_DECL(putwc,     wint_t,   (wchar_t c, FILE *stream));
UNIFYCR_DECL(ungetwc,   wint_t,   (wint_t c, FILE *stream));

#endif /* UNIFYCR_STDIO_H */
