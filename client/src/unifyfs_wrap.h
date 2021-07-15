#ifndef UNIFYFS_WRAP_H
#define UNIFYFS_WRAP_H

#include "config.h"

/* single function to route all unsupported wrapper calls through */
void unifyfs_vunsupported(const char* fn_name,
                          const char* file,
                          int line,
                          const char* fmt,
                          va_list args);

void unifyfs_unsupported(const char* fn_name,
                         const char* file,
                         int line,
                         const char* fmt,
                         ...);

/* Define a macro to indicate unsupported function. Capture function name,
 * file name, and line number along with a user-defined string */
#define UNIFYFS_UNSUPPORTED(fmt, args...) \
    unifyfs_unsupported(__func__, __FILE__, __LINE__, fmt, ##args)


#if UNIFYFS_GOTCHA

/* Definitions to support wrapping functions using Gotcha */

#include <gotcha/gotcha.h>

/* Our wrapper function uses the name __wrap_<iofunc> for <iofunc> */
#define UNIFYFS_WRAP(name) __wrap_##name

/* the name of the real function pointer */
#define UNIFYFS_REAL(name) __real_##name

/* Declare gotcha handle, real function pointer, and our wrapper */
#define UNIFYFS_DECL(name, ret, args)  \
    extern gotcha_wrappee_handle_t wrappee_handle_##name; \
    extern ret (*__real_##name) args; \
    ret __wrap_##name args

/* ask gotcha for the address of the real function */
#define MAP_OR_FAIL(name) \
    do { \
        if (NULL == __real_##name) { \
            __real_##name = gotcha_get_wrappee(wrappee_handle_##name); \
            if (NULL == __real_##name) { \
                assert(!"missing Gotcha wrappee for " #name); \
            } \
        } \
    } while (0)

int setup_gotcha_wrappers(void);

#elif UNIFYFS_PRELOAD

/* ========================================================================
 * Using LD_PRELOAD to wrap functions
 * ========================================================================
 * We need to use the same function names the application is calling, and
 * we then invoke the real function after looking it up with dlsym() */

/* we need the dlsym() function */
#include <dlfcn.h>

/* Our wrapper uses the original function name */
#define UNIFYFS_WRAP(name) name

/* the address of the real open call is stored in __real_open variable */
#define UNIFYFS_REAL(name) __real_##name

/* Declare a static variable called __real_<iofunc> to record the
 * address of the real function and initialize it to NULL */
#define UNIFYFS_DECL(name, ret, args) \
    static ret (*__real_##name) args = NULL;

/* if __real_<iofunc> is still NULL, call dlsym to lookup address of real
 * function and record it */
#define MAP_OR_FAIL(func) \
    if (NULL == __real_##func) { \
        __real_##func = dlsym(RTLD_NEXT, #func); \
        if (NULL == __real_##func) { \
            fprintf(stderr, "UnifyFS failed to map symbol: %s\n", #func); \
            abort(); \
        } \
    }

#else /* Use linker wrapping */

/* ========================================================================
 * Using ld -wrap option to wrap functions
 * ========================================================================
 * The linker converts application calls from <iofunc> --> __wrap_<iofunc>,
 * and renames the wrapped function to __real_<iofunc>. We define our
 * wrapper functions as the __wrap_<iofunc> variant and then to call the
 * real function, we use __real_<iofunc> */

/* Our wrapper function uses the name __wrap_<iofunc> for <iofunc> */
#define UNIFYFS_WRAP(name) __wrap_##name

/* The linker renames the wrapped function to __real_<iofunc> */
#define UNIFYFS_REAL(name) __real_##name

/* Declare the existence of the real function and our wrapper */
#define UNIFYFS_DECL(name, ret, args) \
      extern ret __real_##name args;  \
      ret __wrap_##name args;

/* no need to look up the address of the real function */
#define MAP_OR_FAIL(func)

#endif /* wrapping mode */

#endif /* UNIFYFS_WRAP_H */
