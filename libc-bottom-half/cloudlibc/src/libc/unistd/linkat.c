// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int __wasilibc_nocwd_linkat(int fd1, const char *path1, int fd2, const char *path2, int flag) {
#if defined(__wasip1__)
  // Create lookup properties.
  __wasi_lookupflags_t lookup1_flags = 0;
  if ((flag & AT_SYMLINK_FOLLOW) != 0)
    lookup1_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Perform system call.
  __wasi_errno_t error = __wasi_path_link(fd1, lookup1_flags, path1, fd2, path2);
  if (error != 0) {
    errno = error;
    return -1;
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptors to internal handles
  filesystem_borrow_descriptor_t file_handle1, file_handle2;
  descriptor_table_entry_t entry1;
  if (fd_to_file_handle(fd1, &entry1, &file_handle1) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry1);
  descriptor_table_entry_t entry2;
  if (fd_to_file_handle(fd2, &entry2, &file_handle2) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry2);

  // Convert the strings into WASI strings
  wasi_string_t path1_wasi, path2_wasi;
  if (wasi_string_from_c(path1, &path1_wasi) < 0)
    return -1;
  if (wasi_string_from_c(path2, &path2_wasi) < 0)
    return -1;

  // Create the link
  filesystem_error_code_t error_code;
  filesystem_path_flags_t flags = 0;
  if ((flag & AT_SYMLINK_FOLLOW) != 0)
    flags |= FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW;
  bool ok = filesystem_method_descriptor_link_at(file_handle1,
                                                 flags,
                                                 &path1_wasi,
                                                 file_handle2,
                                                 &path2_wasi,
                                                 &error_code);
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }
#elif defined(__webp2__)
  (void)fd1;
  (void)path1;
  (void)fd2;
  (void)path2;
  (void)flag;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_linkat");
  errno = ENOENT;
  return -1;
#else
# error "Unsupported WASI version"
#endif
  return 0;
}
