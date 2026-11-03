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

#ifdef __wasip1__
static_assert(offsetof(struct iovec, iov_base) ==
                  offsetof(__wasi_iovec_t, buf),
              "Offset mismatch");
static_assert(sizeof(((struct iovec *)0)->iov_base) ==
                  sizeof(((__wasi_iovec_t *)0)->buf),
              "Size mismatch");
static_assert(offsetof(struct iovec, iov_len) ==
                  offsetof(__wasi_iovec_t, buf_len),
              "Offset mismatch");
static_assert(sizeof(((struct iovec *)0)->iov_len) ==
                  sizeof(((__wasi_iovec_t *)0)->buf_len),
              "Size mismatch");
static_assert(sizeof(struct iovec) == sizeof(__wasi_iovec_t),
              "Size mismatch");
#endif

ssize_t readv(int fildes, const struct iovec *iov, int iovcnt) {
  if (iovcnt < 0 || IOV_MAX < iovcnt) {
    errno = EINVAL;
    return -1;
  }
#if defined(__wasip1__)
  size_t bytes_read;
  __wasi_errno_t error = __wasi_fd_read(
      fildes, (const __wasi_iovec_t *)iov, iovcnt, &bytes_read);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return bytes_read;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Skip empty iovecs and then delegate to `read` with the first non-empty
  // iovec.
  while (iovcnt) {
    if (iov->iov_len != 0) {
      return read(fildes, iov->iov_base, iov->iov_len);
    }
    iovcnt--;
    iov++;
  }
  return read(fildes, NULL, 0);
#else
# error "Unsupported WASI version"
#endif
}
