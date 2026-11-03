// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/time.h>
#include <sys/time.h>
#include <wasi/api.h>

int gettimeofday(struct timeval *restrict tp, void *tz) {
  (void) tz;
  if (tp != NULL) {
#if defined(__wasip1__)
    __wasi_timestamp_t ts = 0;
    (void)__wasi_clock_time_get(__WASI_CLOCKID_REALTIME, 1000, &ts);
    *tp = timestamp_to_timeval(ts);
#elif defined(__wasip2__)
    wall_clock_datetime_t time_result;
    wall_clock_now(&time_result);
    *tp = timestamp_to_timeval(&time_result);
#elif defined(__wasip3__)
    system_clock_instant_t time_result;
    system_clock_now(&time_result);
    *tp = timestamp_to_timeval(&time_result);
#elif defined(__webp2__)
    wasilibc_timestamp_t time_result = web_wall_clock_now();
    *tp = timestamp_to_timeval(&time_result);
#else
# error "Unsupported WASI version"
#endif
  }
  return 0;
}
