// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <wasi/file_utils.h>
#include <common/errors.h>
#include <unistd.h>
#endif

ssize_t pwritev(int fildes, const struct iovec *iov, int iovcnt, off_t offset) {
  if (iovcnt < 0 || offset < 0) {
    errno = EINVAL;
    return -1;
  }
#if defined(__wasip1__)
  size_t bytes_written;
  __wasi_errno_t error = __wasi_fd_pwrite(
      fildes, (const __wasi_ciovec_t *)iov, iovcnt, offset, &bytes_written);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return bytes_written;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Skip empty iovecs and then delegate to `pwrite` with the first non-empty
  // iovec.
  while (iovcnt) {
    if (iov->iov_len != 0) {
      return pwrite(fildes, iov->iov_base, iov->iov_len, offset);
    }
    iovcnt--;
    iov++;
  }
  return pwrite(fildes, NULL, 0, offset);
#else
# error "Unsupported WASI version"
#endif
}
