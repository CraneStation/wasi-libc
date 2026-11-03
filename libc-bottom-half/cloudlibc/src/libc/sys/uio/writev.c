// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/uio.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <unistd.h>
#include <wasi/api.h>

#ifndef __wasip1__
#include <wasi/descriptor_table.h>
#include <common/errors.h>
#endif

#ifdef __wasip1__
static_assert(offsetof(struct iovec, iov_base) ==
                  offsetof(__wasi_ciovec_t, buf),
              "Offset mismatch");
static_assert(sizeof(((struct iovec *)0)->iov_base) ==
                  sizeof(((__wasi_ciovec_t *)0)->buf),
              "Size mismatch");
static_assert(offsetof(struct iovec, iov_len) ==
                  offsetof(__wasi_ciovec_t, buf_len),
              "Offset mismatch");
static_assert(sizeof(((struct iovec *)0)->iov_len) ==
                  sizeof(((__wasi_ciovec_t *)0)->buf_len),
              "Size mismatch");
static_assert(sizeof(struct iovec) == sizeof(__wasi_ciovec_t),
              "Size mismatch");
#endif

ssize_t writev(int fildes, const struct iovec *iov, int iovcnt) {
  if (iovcnt < 0 || IOV_MAX < iovcnt) {
    errno = EINVAL;
    return -1;
  }

#if defined(__wasip1__)
  size_t bytes_written = 0;
  __wasi_errno_t error = __wasi_fd_write(
      fildes, (const __wasi_ciovec_t *)iov, iovcnt, &bytes_written);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return bytes_written;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Skip empty iovecs and then delegate to `read` with the first non-empty
  // iovec.
  while (iovcnt) {
    if (iov->iov_len != 0) {
      return write(fildes, iov->iov_base, iov->iov_len);
    }
    iovcnt--;
    iov++;
  }
  return write(fildes, NULL, 0);
#else
# error "Unsupported WASI version"
#endif
}
