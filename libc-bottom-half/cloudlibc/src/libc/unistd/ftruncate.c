// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int ftruncate(int fildes, off_t length) {
  if (length < 0) {
    errno = EINVAL;
    return -1;
  }
#if defined(__wasip1__)
  __wasi_filesize_t st_size = length;
  __wasi_errno_t error =
      __wasi_fd_filestat_set_size(fildes, st_size);
  if (error != 0) {
    errno = error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal file handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  filesystem_error_code_t error_code;
  if (!filesystem_method_descriptor_set_size(file_handle, length, &error_code)) {
    translate_error(&error_code);
    return -1;
  }
#elif defined(__webp2__)
  (void)fildes;
  WASILIBC_LOG_UNSUPPORTED("ftruncate");
  errno = ENOSYS;
  return -1;
#else
# error "Unsupported WASI version"
#endif
  return 0;
}
