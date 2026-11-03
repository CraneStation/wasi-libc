// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int __wasilibc_nocwd_renameat(int oldfd, const char *old, int newfd, const char *new) {
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_path_rename(oldfd, old, newfd, new);
  if (error != 0) {
    errno = error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptors to internal handles
  filesystem_borrow_descriptor_t old_file_handle;
  descriptor_table_entry_t old_entry;
  if (fd_to_file_handle(oldfd, &old_entry, &old_file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(old_entry);

  filesystem_borrow_descriptor_t new_file_handle;
  descriptor_table_entry_t new_entry;
  if (fd_to_file_handle(newfd, &new_entry, &new_file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(new_entry);

  // Convert the strings into WASI strings
  wasi_string_t old_path, new_path;
  if (wasi_string_from_c(old, &old_path) < 0)
    return -1;
  if (wasi_string_from_c(new, &new_path) < 0)
    return -1;

  // Rename the file
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_rename_at(old_file_handle,
                                                   &old_path,
                                                   new_file_handle,
                                                   &new_path,
                                                   &error_code);
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }
#elif defined(__webp2__)
  (void)oldfd;
  (void)old;
  (void)newfd;
  (void)new;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_renameat");
  errno = ENOENT;
  return -1;
#else
# error "Unknown WASI version"
#endif
  return 0;
}
