// Copyright (c) 2015 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <time.h>
#include <wasi/version.h>

int nanosleep(const struct timespec *rqtp, struct timespec *rem) {
#if defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // FIXME(WebAssembly/WASI#857): wasip2/p3 only supports the monotonic clock for
  // sleeping.
  clockid_t clock = CLOCK_MONOTONIC;
#else
  clockid_t clock = CLOCK_REALTIME;
#endif
  int error = clock_nanosleep(clock, 0, rqtp, rem);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
