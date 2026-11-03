// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int __wasilibc_nocwd_mkdirat_nomode(int fd, const char *path) {
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_path_create_directory(fd, path);

  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Create the directory
  filesystem_error_code_t error;
  wasi_string_t path2;
  if (wasi_string_from_c(path, &path2) < 0)
    return -1;
  bool ok = filesystem_method_descriptor_create_directory_at(file_handle,
                                                             &path2,
                                                             &error);
  if (!ok) {
    translate_error(&error);
    return -1;
  }
  return 0;
#elif defined(__webp2__)
  (void)fd;
  (void)path;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_mkdirat_nomode");
  errno = EACCES;
  return -1;
#else
# error "Unsupported WASI version"
#endif
}
