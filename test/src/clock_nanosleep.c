#include "test.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

static void test_elapsed(struct timespec *start_time, struct timespec *end_time,
                         long ns_to_sleep) {
  TEST(end_time->tv_sec - start_time->tv_sec <= 1);

  long nanoseconds_elapsed = (end_time->tv_sec - start_time->tv_sec) * 1E9 -
                             start_time->tv_nsec + end_time->tv_nsec;
  TEST(nanoseconds_elapsed >= ns_to_sleep);
}

#ifndef __wasip1__
static void add_ns(struct timespec *ts, long ns) {
  ts->tv_nsec += ns;
  while (ts->tv_nsec >= 1000000000L) {
    ts->tv_nsec -= 1000000000L;
    ts->tv_sec += 1;
  }
  while (ts->tv_nsec < 0) {
    ts->tv_nsec += 1000000000L;
    ts->tv_sec -= 1;
  }
}
#endif

// Test some basic operations. Note that the actual duration of the sleep is
// not yet tested here since sleeping is currently only relative to the
// monotonic clock, not the `clock` specified, since that's all that WASI
// offers.
static void basic_functionality(clockid_t clock) {
  long ns_to_sleep = 1E6;
  struct timespec time_to_sleep;

  // relative sleep
  time_to_sleep.tv_sec = 0;
  time_to_sleep.tv_nsec = ns_to_sleep;
  TEST(clock_nanosleep(clock, 0, &time_to_sleep, NULL) == 0);

#ifndef __wasip1__
  struct timespec deadline;
  // absolute sleep in the past
  clock_gettime(clock, &deadline);
  add_ns(&deadline, -1000000L);
  TEST(clock_nanosleep(clock, TIMER_ABSTIME, &deadline, NULL) == 0);

  // absolute sleep in the future
  clock_gettime(clock, &deadline);
  add_ns(&deadline, ns_to_sleep);
  TEST(clock_nanosleep(clock, TIMER_ABSTIME, &deadline, NULL) == 0);
#endif

  // Invalid arguments.
  struct timespec invalid = {.tv_sec = 0, .tv_nsec = -1};
  TEST(clock_nanosleep(clock, 0, &invalid, NULL) == EINVAL);
  TEST(clock_nanosleep(clock, TIMER_ABSTIME, &invalid, NULL) == EINVAL);
  invalid.tv_nsec = 1000000000L;
  TEST(clock_nanosleep(clock, 0, &invalid, NULL) == EINVAL);
  TEST(clock_nanosleep(clock, TIMER_ABSTIME, &invalid, NULL) == EINVAL);
  invalid.tv_sec = -1;
  invalid.tv_nsec = 0;
  TEST(clock_nanosleep(clock, 0, &invalid, NULL) == EINVAL);
  TEST(clock_nanosleep(clock, TIMER_ABSTIME, &invalid, NULL) == EINVAL);
  time_to_sleep.tv_sec = 0;
  time_to_sleep.tv_nsec = 0;
  TEST(clock_nanosleep(clock, ~TIMER_ABSTIME, &time_to_sleep, NULL) == EINVAL);
}

int main(void) {
  // Get the clock resolution
  struct timespec clock_resolution;
  clock_getres(CLOCK_MONOTONIC, &clock_resolution);
  // Not much point running the test if the resolution is >= 1 sec
  TEST(clock_resolution.tv_sec == 0);

  // Sleep for a total of 5ms
  long ns_to_sleep = 5E6;
  struct timespec time_to_sleep = {.tv_sec = 0, .tv_nsec = ns_to_sleep};

  struct timespec start_time, end_time;

  clock_gettime(CLOCK_MONOTONIC, &start_time);
  TEST(clock_nanosleep(CLOCK_MONOTONIC, 0, &time_to_sleep, NULL) == 0);
  clock_gettime(CLOCK_MONOTONIC, &end_time);

  test_elapsed(&start_time, &end_time, ns_to_sleep);

  // Sleep for another 50ms to get us pretty far from 5ms from the start of
  // this process to increase the chance the next test fails if it's not
  // implemented correctly.
  time_to_sleep.tv_nsec = 50E6;
  TEST(clock_nanosleep(CLOCK_MONOTONIC, 0, &time_to_sleep, NULL) == 0);

  clock_gettime(CLOCK_MONOTONIC, &time_to_sleep);
  time_to_sleep.tv_nsec += ns_to_sleep;
  if (time_to_sleep.tv_nsec >= 1E9) {
    time_to_sleep.tv_sec += 1;
    time_to_sleep.tv_nsec -= 1E9;
  }
  clock_gettime(CLOCK_MONOTONIC, &start_time);
  TEST(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_to_sleep, NULL) ==
       0);
  clock_gettime(CLOCK_MONOTONIC, &end_time);

  test_elapsed(&start_time, &end_time, ns_to_sleep);

  basic_functionality(CLOCK_MONOTONIC);
  basic_functionality(CLOCK_REALTIME);

  return t_status;
}
