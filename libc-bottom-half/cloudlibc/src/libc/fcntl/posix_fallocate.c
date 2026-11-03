// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <fcntl.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

int posix_fallocate(int fd, off_t offset, off_t len) {
  if (offset < 0 || len < 0)
    return EINVAL;
#if defined(__wasip1__)
  return __wasi_fd_allocate(fd, offset, len);
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  (void) fd;
  WASILIBC_LOG_UNSUPPORTED("posix_fallocate");
  return ENOTSUP;
#else
# error "Unsupported WASI version"
#endif
}
