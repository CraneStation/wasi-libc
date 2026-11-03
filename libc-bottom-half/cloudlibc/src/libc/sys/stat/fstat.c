// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <sys/stat.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>

#if !defined(__wasip1__)
#include <stddefer.h>
#endif

#include "stat_impl.h"

int fstat(int fildes, struct stat *buf) {
#if defined(__wasip1__)
  __wasi_filestat_t internal_stat;
  __wasi_errno_t error = __wasi_fd_filestat_get(fildes, &internal_stat);
  if (error != 0) {
    errno = error;
    return -1;
  }
  to_public_stat(&internal_stat, buf);
  return 0;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Translate the file descriptor to an internal handle
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
  return entry.vtable->fstat(entry.data, buf);
#else
# error "Unsupported WASI version"
#endif
}
