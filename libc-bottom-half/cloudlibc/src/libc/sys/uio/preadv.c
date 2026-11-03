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

ssize_t preadv(int fildes, const struct iovec *iov, int iovcnt, off_t offset) {
  if (iovcnt < 0 || offset < 0) {
    errno = EINVAL;
    return -1;
  }
  size_t bytes_read = 0;
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_fd_pread(
      fildes, (const __wasi_iovec_t *)iov, iovcnt, offset, &bytes_read);
  if (error != 0) {
    errno = error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Skip empty iovecs and then delegate to `pread` with the first non-empty
  // iovec.
  while (iovcnt) {
    if (iov->iov_len != 0) {
      return pread(fildes, iov->iov_base, iov->iov_len, offset);
    }
    iovcnt--;
    iov++;
  }
  return pread(fildes, NULL, 0, offset);
#else
# error "Unsupported WASI version"
#endif
  return bytes_read;
}
