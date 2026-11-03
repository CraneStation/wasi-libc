// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef DIRENT_DIRENT_IMPL_H
#define DIRENT_DIRENT_IMPL_H

#include <__typedef_DIR.h>
#include <stddef.h>
#include <wasi/api.h>

struct dirent;

#define DIRENT_DEFAULT_BUFFER_SIZE 4096

struct _DIR {
  // Directory file descriptor and cookie.
  int fd;
#if defined(__wasip1__)
  __wasi_dircookie_t cookie;

  // Read buffer.
  char *buffer;
  size_t buffer_processed;
  size_t buffer_size;
  size_t buffer_used;
#elif defined(__wasip2__)
  filesystem_own_directory_entry_stream_t stream;
  size_t skip;
  size_t offset;
#elif defined(__wasip3__)
  filesystem_tuple2_stream_directory_entry_future_result_void_error_code_t stream;
  bool stream_done;
  size_t skip;
  size_t offset;
#elif defined(__webp2__)
  // Directories are not supported on webp2.
#else
# error "Unknown WASI version"
#endif

  // Object returned by readdir().
  struct dirent *dirent;
  size_t dirent_size;
};

static inline void dirent_close_streams(DIR *dirp) {
#if defined(__wasip1__) || defined(__webp2__)
  (void) dirp;
  // nothing to close ...
#elif defined(__wasip2__)
  if (dirp->stream.__handle) {
    filesystem_directory_entry_stream_drop_own(dirp->stream);
    dirp->stream.__handle = 0;
  }
#elif defined(__wasip3__)
  if (dirp->stream.f0 != 0) {
    filesystem_stream_directory_entry_drop_readable(dirp->stream.f0);
    dirp->stream.f0 = 0;
    dirp->stream_done = false;
  }
  if (dirp->stream.f1 != 0) {
    filesystem_future_result_void_error_code_drop_readable(dirp->stream.f1);
    dirp->stream.f1 = 0;
  }
#else
# error "Unknown WASI version"
#endif
}

#endif
