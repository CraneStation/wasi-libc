// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/time.h>
#include <wasi/api.h>
#include <time.h>

time_t time(time_t *tloc) {
#if defined(__wasip1__)
  __wasi_timestamp_t ts = 0;
  (void)__wasi_clock_time_get(__WASI_CLOCKID_REALTIME, NSEC_PER_SEC, &ts);
#elif defined(__wasip2__)
  wasilibc_timestamp_t res;
  wall_clock_now(&res);
  uint64_t ts = (res.seconds * NSEC_PER_SEC) + res.nanoseconds;
#elif defined(__wasip3__)
  wasilibc_timestamp_t res;
  system_clock_now(&res);
  time_t ts = (res.seconds * NSEC_PER_SEC) + res.nanoseconds;
#elif defined(__webp2__)
  double epoch_ms = webp2_static_date_now();
  time_t ts = epoch_ms * NSEC_PER_MSEC;
#else
# error "Unsupported WASI version"
#endif
  if (tloc != NULL)
    *tloc = ts / NSEC_PER_SEC;
  return ts / NSEC_PER_SEC;
}
