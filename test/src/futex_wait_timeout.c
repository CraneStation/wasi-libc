#include "test.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>
#include <wasi/libc.h>

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

static volatile int futex_word = 0;

static void inc_ns(struct timespec *ts, long ns) {
  ts->tv_nsec += ns;
  if (ts->tv_nsec >= 1000000000L) {
    ts->tv_nsec -= 1000000000L;
    ts->tv_sec += 1;
  }
  if (ts->tv_nsec < 0) {
    ts->tv_nsec += 1000000000L;
    ts->tv_sec -= 1;
  }
}

int main(void) {

  struct timespec realtime, monotonic;
  TEST(clock_gettime(CLOCK_REALTIME, &realtime) == 0);
  TEST(clock_gettime(CLOCK_MONOTONIC, &monotonic) == 0);

  // Waiting in the past should always timeout immediately, no matter the
  // build.
  inc_ns(&realtime, -1000);
  inc_ns(&monotonic, -1000);

  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_REALTIME,
                             &realtime,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -ETIMEDOUT);
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &monotonic,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -ETIMEDOUT);

  struct timespec short_timeout;
  short_timeout.tv_sec = 0;
  short_timeout.tv_nsec = 100;

  inc_ns(&realtime, 2000);
  inc_ns(&monotonic, 2000);

  // Build that can actually block all time out with short timeouts.
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_REALTIME,
                             &short_timeout, 0) == -ETIMEDOUT);
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &short_timeout, 0) == -ETIMEDOUT);
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_REALTIME,
                             &realtime,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -ETIMEDOUT);
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &monotonic,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -ETIMEDOUT);

  // Invalid futex waits
  struct timespec invalid;
  invalid.tv_sec = -1;
  invalid.tv_nsec = 0;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &invalid, 0) == -EINVAL);
  invalid.tv_sec = 0;
  invalid.tv_nsec = -1;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &invalid, 0) == -EINVAL);
  invalid.tv_sec = 0;
  invalid.tv_nsec = 1000000000;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &invalid, 0) == -EINVAL);
  invalid.tv_sec = monotonic.tv_sec;
  invalid.tv_nsec = -1;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &invalid,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -EINVAL);
  invalid.tv_sec = monotonic.tv_sec;
  invalid.tv_nsec = 1000000000;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_MONOTONIC,
                             &invalid,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -EINVAL);
  invalid.tv_sec = realtime.tv_sec;
  invalid.tv_nsec = -1;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_REALTIME,
                             &invalid,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -EINVAL);
  invalid.tv_sec = realtime.tv_sec;
  invalid.tv_nsec = 1000000000;
  TEST(__wasilibc_futex_wait((volatile void *)&futex_word, 0, CLOCK_REALTIME,
                             &invalid,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -EINVAL);

  volatile void *unaligned_word = (volatile void *)0x12;
  TEST(__wasilibc_futex_wait(unaligned_word, 0, 0, NULL,
                             __WASILIBC_FUTEX_TIMESPEC_ABSOLUTE) == -EINVAL);

  return t_status;
}
