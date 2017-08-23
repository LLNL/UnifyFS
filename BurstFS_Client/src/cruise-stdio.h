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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
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

#ifndef CRUISE_STDIO_H
#define CRUISE_STDIO_H
#include <wchar.h>

#include "cruise-internal.h"

/* TODO: declare the wrapper functions we define in cruise-stdio.c
 * so other routines can call them */
CRUISE_DECL(fclose, int, (FILE *stream));
CRUISE_DECL(fflush, int, (FILE *stream));
CRUISE_DECL(fopen, FILE*, (const char *path, const char *mode));
CRUISE_DECL(freopen, FILE*, (const char *path, const char *mode, FILE *stream));
CRUISE_DECL(setbuf, void*, (FILE *stream, char* buf));
CRUISE_DECL(setvbuf, int, (FILE *stream, char* buf, int type, size_t size));

CRUISE_DECL(fprintf,  int, (FILE* stream, const char* format, ...));
CRUISE_DECL(fscanf,   int, (FILE* stream, const char* format, ...));
CRUISE_DECL(vfprintf, int, (FILE* stream, const char* format, va_list ap));
CRUISE_DECL(vfscanf,  int, (FILE* stream, const char* format, va_list ap));

CRUISE_DECL(fgetc,  int,   (FILE *stream));
CRUISE_DECL(fgets,  char*, (char* s, int n, FILE* stream));
CRUISE_DECL(fputc,  int,   (int c, FILE *stream));
CRUISE_DECL(fputs,  int,   (const char* s, FILE* stream));
CRUISE_DECL(getc,   int,   (FILE *stream));
CRUISE_DECL(putc,   int,   (int c, FILE *stream));
CRUISE_DECL(ungetc, int,   (int c, FILE *stream));

CRUISE_DECL(fread,  size_t, (void *ptr, size_t size, size_t nitems, FILE *stream));
CRUISE_DECL(fwrite, size_t, (const void *ptr, size_t size, size_t nitems, FILE *stream));

CRUISE_DECL(fgetpos, int,  (FILE *stream, fpos_t* pos));
CRUISE_DECL(fseek,   int,  (FILE *stream, long offset,  int whence));
CRUISE_DECL(fsetpos, int,  (FILE *stream, const fpos_t* pos));
CRUISE_DECL(ftell,   long, (FILE *stream));
CRUISE_DECL(rewind,  void, (FILE *stream));

CRUISE_DECL(clearerr, void, (FILE *stream));
CRUISE_DECL(feof,     int,  (FILE *stream));
CRUISE_DECL(ferror,   int,  (FILE *stream));

CRUISE_DECL(fseeko, int,   (FILE *stream, off_t offset, int whence));
CRUISE_DECL(ftello, off_t, (FILE *stream));
CRUISE_DECL(fileno, int,   (FILE *stream));

/* http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf 7.24 */

CRUISE_DECL(fwprintf,  int,      (FILE *stream, const wchar_t* format, ...));
CRUISE_DECL(fwscanf,   int,      (FILE *stream, const wchar_t* format, ...));
CRUISE_DECL(vfwprintf, int,      (FILE *stream, const wchar_t* format, va_list arg));
CRUISE_DECL(vfwscanf,  int,      (FILE *stream, const wchar_t* format, va_list arg));
CRUISE_DECL(fgetwc,    wint_t,   (FILE *stream));
CRUISE_DECL(fgetws,    wchar_t*, (wchar_t* s, int n, FILE *stream));
CRUISE_DECL(fputwc,    wint_t,   (wchar_t wc, FILE *stream));
CRUISE_DECL(fputws,    int,      (const wchar_t* s, FILE *stream));
CRUISE_DECL(fwide,     int,      (FILE *stream, int mode));
CRUISE_DECL(getwc,     wint_t,   (FILE *stream));
CRUISE_DECL(putwc,     wint_t,   (wchar_t c, FILE *stream));
CRUISE_DECL(ungetwc,   wint_t,   (wint_t c, FILE *stream));

#endif /* CRUISE_STDIO_H */
