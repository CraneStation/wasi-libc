// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#include "stat_impl.h"

int __wasilibc_nocwd_utimensat(int fd, const char *path, const struct timespec times[2],
                               int flag) {
#if defined(__wasip1__)
  // Convert timestamps and extract NOW/OMIT flags.
  __wasi_timestamp_t st_atim;
  __wasi_timestamp_t st_mtim;
  __wasi_fstflags_t flags;
  if (!utimens_get_timestamps(times, &st_atim, &st_mtim, &flags)) {
    errno = EINVAL;
    return -1;
  }

  // Create lookup properties.
  __wasi_lookupflags_t lookup_flags = 0;
  if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
    lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Perform system call.
  __wasi_errno_t error =
      __wasi_path_filestat_set_times(fd, lookup_flags, path, st_atim, st_mtim, flags);
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

  // Convert timestamps and extract NOW/OMIT flags.
  filesystem_new_timestamp_t new_timestamp_atim;
  filesystem_new_timestamp_t new_timestamp_mtim;
  if (!utimens_get_timestamps(times, &new_timestamp_atim, &new_timestamp_mtim)) {
    errno = EINVAL;
    return -1;
  }

  // Create lookup properties.
  filesystem_path_flags_t lookup_flags = 0;
  if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
    lookup_flags |= FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW;

  // Convert the string into a Wasm string
  wasi_string_t path_wasm_string;
  if (wasi_string_from_c(path, &path_wasm_string) < 0)
    return -1;

  // Perform system call.
  filesystem_error_code_t error;
  bool set_times_result = filesystem_method_descriptor_set_times_at(file_handle,
                                                                    lookup_flags,
                                                                    &path_wasm_string,
                                                                    &new_timestamp_atim,
                                                                    &new_timestamp_mtim,
                                                                    &error);
  if (!set_times_result) {
    translate_error(&error);
    return -1;
  }
#elif defined(__webp2__)
  (void)fd;
  (void)path;
  (void)times;
  (void)flag;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_utimensat");
  errno = ENOENT;
  return -1;
#else
# error "Unsupported WASI version"
#endif
  return 0;
}
