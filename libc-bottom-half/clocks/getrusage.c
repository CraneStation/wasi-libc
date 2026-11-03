#define _WASI_EMULATED_PROCESS_CLOCKS
#include <common/time.h>
#include <errno.h>
#include <sys/resource.h>
#include <time.h>
#include <wasi/api.h>

// `clock` is a weak symbol so that application code can override it.
// We want to use the function in libc, so use the libc-internal name.
clock_t __clock(void);

int getrusage(int who, struct rusage *r_usage) {
  switch (who) {
  case RUSAGE_SELF: {
#if defined(__wasip1__)
    __wasi_timestamp_t usertime = __clock();
    *r_usage = (struct rusage){.ru_utime = timestamp_to_timeval(usertime)};
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
    clock_t usertime = __clock();
    *r_usage = (struct rusage){.ru_utime = instant_to_timeval(usertime)};
#else
#error "Unknown WASI version"
#endif
    return 0;
  }
  case RUSAGE_CHILDREN:
    *r_usage = (struct rusage){};
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}
