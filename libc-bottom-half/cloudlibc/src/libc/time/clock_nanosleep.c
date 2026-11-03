// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>
#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <time.h>
#include <wasi/wasip3_block.h>

#ifndef __wasip1__
#include <stddefer.h>
#endif

#ifdef __wasip1__
static_assert(TIMER_ABSTIME == __WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME,
              "Value mismatch");
#endif

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp,
                    struct timespec *rmtp) {
  (void) rmtp;
  if ((flags & ~TIMER_ABSTIME) != 0)
    return EINVAL;
  if (rqtp->tv_nsec >= 1000000000L || rqtp->tv_nsec < 0 || rqtp->tv_sec < 0)
    return EINVAL;

#if defined(__wasip1__)
  // Prepare polling subscription.
  __wasi_subscription_t sub = {
      .u.tag = __WASI_EVENTTYPE_CLOCK,
      .u.u.clock.id = clock_id->id,
      .u.u.clock.flags = flags,
  };
  if (!timespec_to_timestamp_clamp(rqtp, &sub.u.u.clock.timeout))
    return EINVAL;

  // Block until polling event is triggered.
  size_t nevents;
  __wasi_event_t ev;
  __wasi_errno_t error = __wasi_poll_oneoff(&sub, &ev, 1, &nevents);
  return error == 0 && ev.error == 0 ? 0 : ENOTSUP;
#elif defined(__wasip2__) || defined(__wasip3__)

  // WASIp{2,3} only provide methods of blocking on a monotonic clock, so fudge
  // things a bit here. Relative sleeps on other clocks become sleeps on the
  // monotonic clock which in theory should be close enough. Absolute sleeps
  // require getting the current time on the requested clock, converting it to
  // a relative duration, and then sleeping that relative amount on the
  // monotonic clock.
  //
  // This isn't 100% accurate but it's the best that can be done with the
  // APIs on WASIp{2,3} it seems.
  const struct timespec *at = rqtp;
  struct timespec rel;
  if (clock_id->id != CLOCKID_MONOTONIC && (flags & TIMER_ABSTIME)) {
    int prev_errno = errno;
    defer errno = prev_errno;
    if (__clock_gettime(clock_id, &rel) != 0)
      return errno;
    rel.tv_sec = at->tv_sec - rel.tv_sec;
    if ((rel.tv_nsec = at->tv_nsec - rel.tv_nsec) < 0) {
      rel.tv_sec--;
      rel.tv_nsec += 1000000000;
    }
    // If the absolute time was in the past, then clamp to a 0-duration sleep.
    if (rel.tv_sec < 0) {
      rel.tv_sec = 0;
      rel.tv_nsec = 0;
    }
    at = &rel;
    flags &= ~TIMER_ABSTIME;
  }

  uint64_t duration;
  if (__builtin_mul_overflow((uint64_t)at->tv_sec, NSEC_PER_SEC, &duration) ||
      __builtin_add_overflow(duration, (uint64_t)at->tv_nsec, &duration))
    duration = UINT64_MAX;

#ifdef __wasip2__
  // Prepare pollable
  monotonic_clock_own_pollable_t pollable;
  if (flags & TIMER_ABSTIME) {
    pollable = monotonic_clock_subscribe_instant(duration);
  } else {
    pollable = monotonic_clock_subscribe_duration(duration);
  }

  // Block until polling event is triggered.
  poll_method_pollable_block(poll_borrow_pollable(pollable));

  poll_pollable_drop_own(pollable);
#else // defined(__wasip3__)
  if (flags & TIMER_ABSTIME) {
    __wasilibc_subtask_await(monotonic_clock_wait_until(duration));
  } else {
    __wasilibc_subtask_await(monotonic_clock_wait_for(duration));
  }
#endif
  return 0;

#else
# error "Unsupported WASI version"
#endif
}

weak_alias(clock_nanosleep, __clock_nanosleep);
