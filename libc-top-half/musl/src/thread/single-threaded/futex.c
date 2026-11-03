#include <wasi/libc.h>
#include <wasi/api.h>
#include "common/time.h"
#include <errno.h>
#include <time.h>

int __wasilibc_futex_wait(volatile int *addr, int val, clockid_t clk,
                          const struct timespec *at, unsigned flags)
{
  if ((flags & ~__WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) != 0) {
    return -EINVAL;
  }
  if ((((intptr_t)addr) & 3) != 0) {
    return -EINVAL;
  }
  if (*addr != val) {
    return -EWOULDBLOCK;
  }

  // If this operation has a timeout, then without a thread we'll never receive
  // a notification. Translate this to a sleep call in such a situation.
  if (at) {
    int flags2 = 0;
    if (flags & __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) {
      // If `tv_sec` points into the past with an absolute deadline then that
      // timeout has already elapsed so return as such.
      if (at->tv_sec < 0 && at->tv_nsec >= 0 && at->tv_nsec < 1000000000L)
        return -ETIMEDOUT;
      flags2 |= TIMER_ABSTIME;
    }
    int r = __clock_nanosleep(clk, flags2, at, NULL);
    if (r == 0)
      return -ETIMEDOUT;
    return -r;
  }

  // If this operation has no timeout then no other thread will ever wake this
  // up, so instead of blocking forever just trap.
  __builtin_trap();
}

int __wasilibc_futex_wake(volatile int *addr, int count, unsigned flags)
{
  (void)addr;
  (void)count;
  if (flags != 0 && flags != __WASILIBC_FUTEX_YIELD) {
    return -EINVAL;
  }
  // no-op, by definition there can't be any waiting threads.
  return 0;
}
