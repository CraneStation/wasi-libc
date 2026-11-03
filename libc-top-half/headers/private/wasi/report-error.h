#ifndef __WASI_REPORT_ERROR_H
#define __WASI_REPORT_ERROR_H

#include <features.h>
#include <wasi/api.h>

// Reports an error using the system's typical error output.
hidden void __wasilibc_report_error(const char* message);
hidden void __wasilibc_report_errorf(const char *format, ...)
    __attribute__((__format__(__printf__, 1, 2)));

#ifdef WASILIBC_LOG_UNSUPPORTED_CALLS
#define WASILIBC_LOG_UNSUPPORTED(name) __wasilibc_report_error(name " is not supported");
#else
#define WASILIBC_LOG_UNSUPPORTED(name)
#endif

#ifdef __webp2__
// Reports an error to the console using webp2 bindings.
void __wasilibc_webp2_report_error(const char* message);
#endif

#endif // __WASI_REPORT_ERROR_H
