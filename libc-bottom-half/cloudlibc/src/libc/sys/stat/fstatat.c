// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#include "stat_impl.h"

int __wasilibc_nocwd_fstatat(int fd, const char *restrict path, struct stat *restrict buf,
                             int flag) {
  if (path[0] == '\0') {
    // With AT_EMPTY_PATH an empty path refers to fd itself; otherwise the
    // WASI path-relative stat calls cannot resolve it, so reject as ENOENT.
    if (flag & AT_EMPTY_PATH)
      return fstat(fd, buf);
    errno = ENOENT;
    return -1;
  }

#if defined(__wasip1__)
  __wasi_lookupflags_t lookup_flags = 0;
  if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
      lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Perform system call.
  __wasi_filestat_t internal_stat;
  __wasi_errno_t error =
      __wasi_path_filestat_get(fd, lookup_flags, path, &internal_stat);
  if (error != 0) {
      errno = error;
      return -1;
  }
  to_public_stat(&internal_stat, buf);

  return 0;
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Convert the string into a Wasm string
  wasi_string_t path_wasm_string;
  if (wasi_string_from_c(path, &path_wasm_string) < 0)
    return -1;

  // Get the metadata hash for this file
  filesystem_metadata_hash_value_t metadata;
  filesystem_error_code_t error;
  filesystem_path_flags_t path_flags = (flag | AT_SYMLINK_NOFOLLOW) ? 0
    : FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW;
  if (!filesystem_method_descriptor_metadata_hash_at(file_handle,
                                                     path_flags,
                                                     &path_wasm_string,
                                                     &metadata,
                                                     &error)) {
    translate_error(&error);
    return -1;
  }

  // Create lookup properties.
  filesystem_path_flags_t lookup_flags = 0;
  if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
    lookup_flags |= FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW;

  // Perform system call.
  filesystem_descriptor_stat_t internal_stat;
  bool stat_result = filesystem_method_descriptor_stat_at(file_handle,
                                                          lookup_flags,
                                                          &path_wasm_string,
                                                          &internal_stat,
                                                          &error);
  if (!stat_result) {
    translate_error(&error);
    return -1;
  }

  // Convert the internal data to an external struct
  to_public_stat(&metadata, &internal_stat, buf);
  return 0;
#elif defined(__webp2__)
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_fstatat");
  errno = ENOENT;
  return -1;
#else
# error "Unsupported WASI version"
#endif
}
