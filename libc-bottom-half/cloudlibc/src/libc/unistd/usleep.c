// Copyright (c) 2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <wasi/version.h>

int usleep(useconds_t useconds) {
  struct timespec ts = {.tv_sec = useconds / 1000000,
                        .tv_nsec = useconds % 1000000 * 1000};
#if defined(__wasip1__)
  clockid_t clock_id = CLOCK_REALTIME;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // FIXME(WebAssembly/WASI#857): wasip2/p3 only supports the monotonic clock
  // for sleeping.
  clockid_t clock_id = CLOCK_MONOTONIC;
#endif
  int error = clock_nanosleep(clock_id, 0, &ts, NULL);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
