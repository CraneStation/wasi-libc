// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMMON_TIME_H
#define COMMON_TIME_H

#include <common/limits.h>

#include <sys/time.h>

#include <math.h>
#include <wasi/api.h>
#include <stdbool.h>
#include <time.h>

#define NSEC_PER_SEC 1000000000
#define NSEC_PER_MSEC 1000000
#define NSEC_PER_USEC 1000
#define USEC_PER_SEC 1000000
#define USEC_PER_MSEC 1000
#define MSEC_PER_SEC 1000

#if defined(__wasip1__)
typedef __wasi_timestamp_t wasilibc_timestamp_t;
#elif defined(__wasip2__)
typedef wall_clock_datetime_t wasilibc_timestamp_t;
#elif defined(__wasip3__)
typedef filesystem_instant_t wasilibc_timestamp_t;
typedef monotonic_clock_mark_t monotonic_clock_instant_t;
#elif defined(__webp2__)
typedef struct wasilibc_timestamp_t {
  uint64_t   seconds;
  uint32_t   nanoseconds;
} wasilibc_timestamp_t;
typedef uint64_t monotonic_clock_instant_t; // nanoseconds
typedef uint64_t monotonic_clock_duration_t; // nanoseconds
#else
# error "Unknown WASI version"
#endif

static inline bool timespec_to_timestamp_exact(
  const struct timespec *timespec, wasilibc_timestamp_t *timestamp) {
  // Invalid nanoseconds field.
  if (timespec->tv_nsec < 0 || timespec->tv_nsec >= NSEC_PER_SEC)
    return false;

#if defined(__wasip1__) || defined(__wasip2__) || defined(__webp2__)
  // Timestamps before the Epoch are not supported.
  if (timespec->tv_sec < 0)
    return false;
#endif

#if defined(__wasip1__)
  // Make sure our timestamp does not overflow.
  return !__builtin_mul_overflow(timespec->tv_sec, NSEC_PER_SEC, timestamp) &&
         !__builtin_add_overflow(*timestamp, timespec->tv_nsec, timestamp);
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  timestamp->seconds = timespec->tv_sec;
  timestamp->nanoseconds = timespec->tv_nsec;
  return true;
#else
# error "Unknown WASI version"
#endif
}

static inline bool timespec_to_timestamp_clamp(
  const struct timespec *timespec, wasilibc_timestamp_t *timestamp) {
  // Invalid nanoseconds field.
  if (timespec->tv_nsec < 0 || timespec->tv_nsec >= NSEC_PER_SEC)
    return false;

#if defined(__wasip1__)
  if (timespec->tv_sec < 0) {
    // Timestamps before the Epoch are not supported.
    *timestamp = 0;
  } else if (__builtin_mul_overflow(timespec->tv_sec, NSEC_PER_SEC, timestamp) ||
             __builtin_add_overflow(*timestamp, timespec->tv_nsec, timestamp)) {
    // Make sure our timestamp does not overflow.
    *timestamp = NUMERIC_MAX(__wasi_timestamp_t);
  }
#elif defined(__wasip2__) || defined(__webp2__)
  if (timespec->tv_sec < 0) {
    // Timestamps before the Epoch are not supported.
    timestamp->seconds = 0;
    timestamp->nanoseconds = 0;
  } else {
    timestamp->seconds = timespec->tv_sec;
    timestamp->nanoseconds = timespec->tv_nsec;
  }
#elif defined(__wasip3__)
    timestamp->seconds = timespec->tv_sec;
    timestamp->nanoseconds = timespec->tv_nsec;
#else
# error "Unknown WASI version"
#endif
  return true;
}

#if defined(__wasip1__)

static inline struct timespec timestamp_to_timespec(
  __wasi_timestamp_t timestamp) {
  // Decompose timestamp into seconds and nanoseconds.
  return (struct timespec){.tv_sec = timestamp / NSEC_PER_SEC,
                           .tv_nsec = timestamp % NSEC_PER_SEC};
}

static inline struct timeval timestamp_to_timeval(
  __wasi_timestamp_t timestamp) {
  struct timespec ts = timestamp_to_timespec(timestamp);
  return (struct timeval){.tv_sec = ts.tv_sec, ts.tv_nsec / 1000};
}

#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)

static inline struct timespec timestamp_to_timespec(
  wasilibc_timestamp_t *timestamp) {
#if defined(__wasip2__) || defined(__webp2__)
  // Check for overflow when converting unsigned to signed
  if (timestamp->seconds > INT64_MAX) {
    return (struct timespec){.tv_sec = INT64_MAX, .tv_nsec = NSEC_PER_SEC - 1};
  }
#endif
  return (struct timespec){.tv_sec = timestamp->seconds,
                           .tv_nsec = timestamp->nanoseconds};
}

static inline struct timespec instant_to_timespec(
  monotonic_clock_instant_t ns) {
    // Decompose instant into seconds and nanoseconds
  return (struct timespec){.tv_sec = ns / NSEC_PER_SEC,
                           .tv_nsec = ns % NSEC_PER_SEC};
}

static inline bool timespec_to_instant_clamp(
  const struct timespec* timespec, monotonic_clock_instant_t* instant) {

  // Invalid nanoseconds field
  if (timespec->tv_nsec < 0 || timespec->tv_nsec >= NSEC_PER_SEC)
    return false;
  if (timespec->tv_sec < 0) {
    // Timestamps before the Epoch are not supported
    *instant = 0;
  } else if (__builtin_mul_overflow(timespec->tv_sec, NSEC_PER_SEC, instant) ||
             __builtin_add_overflow(*instant, timespec->tv_nsec, instant)) {
   // Make sure our timestamp does not overflow
   *instant = NUMERIC_MAX(monotonic_clock_instant_t);
  }
  return true;
}

static inline struct timeval instant_to_timeval(
  monotonic_clock_instant_t ns) {
    // Decompose instant into seconds and microoseconds
  return (struct timeval){.tv_sec = ns / NSEC_PER_SEC,
                          .tv_usec = (ns % NSEC_PER_SEC) / 1000};
}

static inline struct timeval duration_to_timeval(
  monotonic_clock_duration_t ns) {
    // Decompose duration into seconds and microoseconds
  return (struct timeval){.tv_sec = ns / NSEC_PER_SEC,
                          .tv_usec = (ns % NSEC_PER_SEC) / 1000};
}

static inline bool timeval_to_duration(
  struct timeval* timeval, monotonic_clock_duration_t* duration) {
    // Invalid microseconds field
    if (timeval->tv_usec < 0 || timeval->tv_usec >= USEC_PER_SEC)
      return false;
    if (timeval->tv_sec < 0) {
      // Timestamps before the Epoch are not supported
      *duration = 0;
    } else if (__builtin_mul_overflow(timeval->tv_sec, NSEC_PER_SEC, duration) ||
               __builtin_add_overflow(*duration, timeval->tv_usec * 1000, duration)) {
      // Make sure our duration does not overflow
      *duration = NUMERIC_MAX(monotonic_clock_instant_t);
    }
    return true;
}

static inline struct timeval timestamp_to_timeval(
  wasilibc_timestamp_t *timestamp) {
  #if defined(__wasip2__)
  // Check for overflow when converting unsigned to signed
  if (timestamp->seconds > INT64_MAX) {
    return (struct timeval){.tv_sec = INT64_MAX, .tv_usec = USEC_PER_SEC - 1};
  }
  #endif
  return (struct timeval){.tv_sec = timestamp->seconds,
                          .tv_usec = timestamp->nanoseconds / 1000};
}
#else
# error "Unknown WASI version"
#endif

#ifdef __webp2__
static inline wasilibc_timestamp_t web_wall_clock_now() {
  double clock_ms = webp2_static_date_now();
  return (wasilibc_timestamp_t){
    .seconds = (uint64_t)(clock_ms / MSEC_PER_SEC),
    .nanoseconds = fmod(clock_ms, MSEC_PER_SEC) * NSEC_PER_MSEC,
  };
}

static inline monotonic_clock_instant_t web_monotonic_clock_now() {
  webp2_own_performance_t perf = webp2_get_global_performance();
  double time_origin = webp2_method_get_performance_time_origin(webp2_borrow_performance(perf));
  double clock_ms = time_origin + webp2_method_performance_now(webp2_borrow_performance(perf));
  webp2_performance_drop_own(perf);
  return (monotonic_clock_instant_t)(clock_ms * NSEC_PER_MSEC);
}
#endif

#endif
