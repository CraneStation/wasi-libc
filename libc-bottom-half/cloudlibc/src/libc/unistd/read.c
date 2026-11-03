// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>

#if !defined(__wasip1__)
#include <stddefer.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#include <string.h>
#include "lock.h"
#endif

ssize_t read(int fildes, void *buf, size_t nbyte) {
#if defined(__wasip1__)
  __wasi_iovec_t iov = {.buf = buf, .buf_len = nbyte};
  size_t bytes_read;
  __wasi_errno_t error = __wasi_fd_read(fildes, &iov, 1, &bytes_read);
  if (error != 0) {
    errno = error == ENOTCAPABLE ? EBADF : error;
    return -1;
  }
  return bytes_read;
#else
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
#if defined(__wasip2__) || defined(__wasip3__)
  if (entry.vtable->get_read_stream) {
    wasi_read_t read;
    if (entry.vtable->get_read_stream(entry.data, &read) < 0)
      return -1;
    defer STRONG_UNLOCK(*read.state->lock);
    return __wasilibc_read(&read, buf, nbyte);
  }
#elif defined(__webp2__)
  if (entry.vtable->read) {
    return entry.vtable->read(entry.data, buf, nbyte);
  }
#else
#error "Unknown WASI version"
#endif
  if (entry.vtable->recvfrom)
    return entry.vtable->recvfrom(entry.data, buf, nbyte, 0, NULL, NULL);
  errno = EOPNOTSUPP;
  return -1;
#endif
}
