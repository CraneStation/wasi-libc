// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int __wasilibc_nocwd_symlinkat(const char *path1, int fd, const char *path2) {
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_path_symlink(path1, fd, path2);
  if (error != 0) {
    errno = error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Convert the paths into WASI paths
  wasi_string_t path1_wasi, path2_wasi;
  if (wasi_string_from_c(path1, &path1_wasi) < 0)
    return -1;
  if (wasi_string_from_c(path2, &path2_wasi) < 0)
    return -1;

  // Construct the link
  filesystem_error_code_t error_code;
  // path1 is the path for the existing file; path2 is the path for the new link
  bool ok = filesystem_method_descriptor_symlink_at(file_handle,
                                                    &path1_wasi,
                                                    &path2_wasi,
                                                    &error_code);
  // Check for errors
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }
#elif defined(__webp2__)
  (void)path1;
  (void)fd;
  (void)path2;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_symlinkat");
  errno = EACCES;
  return -1;
#else
# error "Unsupported WASI version"
#endif

  return 0;
}
