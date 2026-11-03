// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int fdatasync(int fildes) {
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_fd_datasync(fildes);
  if (error != 0) {
    errno = error == ENOTCAPABLE ? EBADF : error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Sync the data
  filesystem_error_code_t error_code;
  if (!filesystem_method_descriptor_sync_data(file_handle, &error_code)) {
    translate_error(&error_code);
    return -1;
  }
#elif defined(__webp2__)
  (void)fildes;
  WASILIBC_LOG_UNSUPPORTED("fdatasync");
  errno = EINVAL;
  return -1;
#else
# error "Unsupported WASI version"
#endif
  return 0;
}
