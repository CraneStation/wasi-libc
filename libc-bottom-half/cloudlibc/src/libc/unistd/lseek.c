// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>

#if !defined(__wasip1__)
#include <stddefer.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#ifdef __wasip1__
static_assert(SEEK_CUR == __WASI_WHENCE_CUR, "Value mismatch");
static_assert(SEEK_END == __WASI_WHENCE_END, "Value mismatch");
static_assert(SEEK_SET == __WASI_WHENCE_SET, "Value mismatch");
#endif

off_t __lseek(int fildes, off_t offset, int whence) {
#if defined(__wasip1__)
  __wasi_filesize_t new_offset;
  __wasi_errno_t error =
      __wasi_fd_seek(fildes, offset, whence, &new_offset);
  if (error != 0) {
    errno = error == ENOTCAPABLE ? ESPIPE : error;
    return -1;
  }
  return new_offset;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Look up a stream for fildes
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
  if (!entry.vtable->seek) {
    errno = ESPIPE;
    return -1;
  }
  return entry.vtable->seek(entry.data, offset, whence);
#else
# error "Unknown WASI version"
#endif
}

weak_alias(__lseek, lseek);
