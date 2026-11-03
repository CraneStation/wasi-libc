// Copyright (c) 2015 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <__errno_values.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wasi/report-error.h>
#include <wasi/version.h>

unsigned int sleep(unsigned int seconds) {
  struct timespec ts = {.tv_sec = seconds, .tv_nsec = 0};
  // FIXME(WebAssembly/WASI#857): wasip2/p3 only supports the monotonic clock for
  // sleeping.
#if defined(__wasip1__)
  clockid_t clock_id = CLOCK_REALTIME;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  clockid_t clock_id = CLOCK_MONOTONIC;
#else
# error "Unsupported WASI version"
#endif
  int err = clock_nanosleep(clock_id, 0, &ts, NULL);
#ifdef __webp2__
  if (err == ENOSYS) {
    // If we return `seconds` from this, we risk user code going into an
    // infinite loop. Therefore we noisily exit. clock_nanosleep has already
    // logged.
    exit(1);
  }
#endif
  if (err != 0)
    return seconds;
  return 0;
}
