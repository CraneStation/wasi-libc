// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>
#include <math.h>
#include <wasi/api.h>
#include <errno.h>
#include <time.h>

int __clock_gettime(clockid_t clock_id, struct timespec *tp) {
#if defined(__wasip1__)
  __wasi_timestamp_t ts;
  __wasi_errno_t error = __wasi_clock_time_get(clock_id->id, 1, &ts);
  if (error != 0) {
    errno = error;
    return -1;
  }
  *tp = timestamp_to_timespec(ts);
#elif defined(__wasip2__) || defined(__wasip3__)
  if (tp == NULL)
    return 0;

  if (clock_id->id == CLOCKID_MONOTONIC) {
    monotonic_clock_instant_t ns = monotonic_clock_now();
    *tp = instant_to_timespec(ns);
  } else if (clock_id->id == CLOCKID_REALTIME) {
    wasilibc_timestamp_t time_result;
    #ifdef __wasip2__
    wall_clock_now(&time_result);
    #else
    system_clock_now(&time_result);
    #endif
    *tp = timestamp_to_timespec(&time_result);
  } else {
    errno = EINVAL;
    return -1; // wasip{2,3} only supports wall and monotonic clocks
  }
#elif defined(__webp2__)
  if (tp == NULL) {
    return 0;
  }

  if (clock_id->id == CLOCKID_MONOTONIC) {
    *tp = instant_to_timespec(web_monotonic_clock_now());
  } else if (clock_id->id == CLOCKID_REALTIME) {
    wasilibc_timestamp_t now = web_wall_clock_now();
    *tp = timestamp_to_timespec(&now);
  } else {
    errno = EINVAL;
    return -1;
  }
#else
# error "Unsupported WASI version"
#endif
  return 0;
}
weak_alias(__clock_gettime, clock_gettime);
