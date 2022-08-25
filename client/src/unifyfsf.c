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

/* This file compiles a normalized interface for Fortran in which:
 *   - Fortran link names are in lower case
 *   - Fortran link names have a single trailing underscore
 *   - boolean true is expected to be 1
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "unifyfs.h"

/* TODO: enable UNIFYFS_Fint to be configured to be different type */
typedef int UNIFYFS_Fint;

#ifdef USE_FORT_STDCALL
# define FORT_CALL __stdcall
#elif defined (USE_FORT_CDECL)
# define FORT_CALL __cdecl
#else
# define FORT_CALL
#endif

#ifdef USE_FORT_MIXED_STR_LEN
# define FORT_MIXED_LEN_DECL   , UNIFYFS_Fint
# define FORT_END_LEN_DECL
# define FORT_MIXED_LEN(a)     , UNIFYFS_Fint a
# define FORT_END_LEN(a)
#else
# define FORT_MIXED_LEN_DECL
# define FORT_END_LEN_DECL     , UNIFYFS_Fint
# define FORT_MIXED_LEN(a)
# define FORT_END_LEN(a)       , UNIFYFS_Fint a
#endif

#ifdef HAVE_FORTRAN_API
# ifdef FORTRAN_EXPORTS
#  define FORTRAN_API __declspec(dllexport)
# else
#  define FORTRAN_API __declspec(dllimport)
# endif
#else
# define FORTRAN_API
#endif

/* convert a Fortran string to a C string, by removing any trailing
 * spaces and terminating with a NULL */
static int unifyfs_fstr2cstr(const char* fstr, int flen, char* cstr, int clen)
{
    int rc = 0;

    /* check that our pointers aren't NULL */
    if ((fstr == NULL) || (cstr == NULL)) {
        return 1;
    }

    /* determine length of Fortran string after removing trailing spaces */
    while ((flen > 0) && (fstr[flen-1] == ' ')) {
        flen--;
    }

    /* assume we can copy the whole string */
    int len = flen;
    if (flen > (clen - 1)) {
        /* Fortran string is longer than C buffer, trucnated copy */
        len = clen - 1;
        rc = 1;
    }

    /* copy the Fortran string to the C string */
    if (len > 0) {
        strncpy(cstr, fstr, len);
    }

    /* null-terminate the C string */
    if (len >= 0) {
        cstr[len] = '\0';
    }

    return rc;
}

/*
 * Marking unifyfs_cstr2fstr() with an '#if 0' block, since this function
 * isn't used yet.
 */
#if 0
/* convert a C string to a Fortran string, adding trailing spaces
 * as necessary */
static int unifyfs_cstr2fstr(const char* cstr, char* fstr, int flen)
{
    int rc = 0;

    /* check that our pointers aren't NULL */
    if ((cstr == NULL) || (fstr == NULL)) {
        return 1;
    }

    /* determine length of C string */
    int clen = strlen(cstr);

    /* copy the characters from the Fortran string to the C string */
    if (clen <= flen) {
        /* C string will fit within our Fortran buffer, copy it over */
        if (clen > 0) {
            strncpy(fstr, cstr, clen);
        }

        /* fill in trailing spaces */
        while (clen < flen) {
            fstr[clen] = ' ';
            clen++;
        }
    } else {
        /* C string is longer than Fortran buffer, truncated copy */
        strncpy(fstr, cstr, flen);
        rc = 1;
    }

    return rc;
}
#endif

/*================================================
 * Mount, Unmount
 *================================================*/

FORTRAN_API
void FORT_CALL unifyfs_mount_(char* prefix FORT_MIXED_LEN(prefix_len),
                              int* rank, int* size,
                              int* ierror
                              FORT_END_LEN(prefix_len))
{
    /* convert name from a Fortran string to C string */
    char prefix_tmp[1024];
    int rc = unifyfs_fstr2cstr(prefix, prefix_len,
                               prefix_tmp, sizeof(prefix_tmp));
    if (rc != 0) {
        *ierror = 1; // !UNIFYFS_SUCCESS
        return;
    }

    int rank_tmp = *rank;
    int size_tmp = *size;
    *ierror = unifyfs_mount(prefix_tmp, rank_tmp, size_tmp);
    return;
}

FORTRAN_API
void FORT_CALL unifyfs_unmount_(int* ierror)
{
    *ierror = unifyfs_unmount();
    return;
}
