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

ssize_t __wasilibc_nocwd_readlinkat(int fd, const char *restrict path, char *restrict buf,
                                    size_t bufsize) {
  size_t bufused;
#if defined(__wasip1__)
  // TODO: Remove the cast on `buf` once the witx is updated with char8 support.
  __wasi_errno_t error = __wasi_path_readlink(fd, path,
                                                      (uint8_t*)buf, bufsize, &bufused);
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

  // Convert the path into a WASI path
  wasi_string_t wasi_path, link_source;
  if (wasi_string_from_c(path, &wasi_path) < 0)
    return -1;

  // Read the link
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_readlink_at(file_handle,
                                                     &wasi_path,
                                                     &link_source,
                                                     &error_code);
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }

  // Copy the contents of the output path back into the buffer provided
  bufused = bufsize < link_source.len ? bufsize : link_source.len;
  memcpy(buf, link_source.ptr, bufused);
#ifdef __wasip2__
  wasip2_string_free(&link_source);
#else
  wasip3_string_free(&link_source);
#endif
#elif defined(__webp2__)
  (void)fd;
  (void)path;
  (void)buf;
  (void)bufsize;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_readlinkat");
  errno = ENOENT;
  return -1;
#else
# error "Unsupported WASI version"
#endif
  return bufused;
}
