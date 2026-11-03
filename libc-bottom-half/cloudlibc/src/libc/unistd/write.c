// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <unistd.h>
#include <wasi/api.h>

#if !defined(__wasip1__)
#include <stddefer.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#include <time.h>
#include "lock.h"
#endif

ssize_t write(int fildes, const void *buf, size_t nbyte) {
#if defined(__wasip1__)
  __wasi_ciovec_t iov = {.buf = buf, .buf_len = nbyte};
  size_t bytes_written;
  __wasi_errno_t error =
      __wasi_fd_write(fildes, &iov, 1, &bytes_written);
  if (error != 0) {
    errno = error == ENOTCAPABLE ? EBADF : error;
    return -1;
  }
  return bytes_written;
#else
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
#if defined(__wasip2__) || defined(__wasip3__)
  if (entry.vtable->get_write_stream) {
    wasi_write_t write;
    if (entry.vtable->get_write_stream(entry.data, &write) < 0)
      return -1;
    defer STRONG_UNLOCK(*write.state->lock);
    return __wasilibc_write(&write, buf, nbyte);
  }
#elif defined(__webp2__)
  if (entry.vtable->write) {
    return entry.vtable->write(entry.data, buf, nbyte);
  }
#else
#error "Unknown WASI version"
#endif
  if (entry.vtable->sendto)
    return entry.vtable->sendto(entry.data, buf, nbyte, 0, NULL, 0);
  errno = EOPNOTSUPP;
  return -1;
#endif
}
