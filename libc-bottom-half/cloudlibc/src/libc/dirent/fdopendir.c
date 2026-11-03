// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#include "dirent_impl.h"

DIR *fdopendir(int fd) {
  // Allocate new directory object and read buffer.
  DIR *dirp = malloc(sizeof(*dirp));
  if (dirp == NULL)
    return NULL;

#if defined(__wasip1__)
  dirp->buffer = malloc(DIRENT_DEFAULT_BUFFER_SIZE);
  if (dirp->buffer == NULL) {
    free(dirp);
    return NULL;
  }

  // Ensure that this is really a directory by already loading the first
  // chunk of data.
  __wasi_errno_t error =
      // TODO: Remove the cast on `dirp->buffer` once the witx is updated with char8 support.
      __wasi_fd_readdir(fd, (uint8_t *)dirp->buffer, DIRENT_DEFAULT_BUFFER_SIZE,
                                __WASI_DIRCOOKIE_START, &dirp->buffer_used);
  if (error != 0) {
    free(dirp->buffer);
    free(dirp);
    errno = error;
    return NULL;
  }

  // Initialize other members.
  dirp->fd = fd;
  dirp->cookie = __WASI_DIRCOOKIE_START;
  dirp->buffer_processed = 0;
  dirp->buffer_size = DIRENT_DEFAULT_BUFFER_SIZE;
  dirp->dirent = NULL;
  dirp->dirent_size = 1;
  return dirp;
#elif defined(__wasip2__) || defined(__wasip3__)
  defer free(dirp);

  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return NULL;
  defer descriptor_table_entry_dec(entry);

  // Read the directory
#if defined(__wasip2__)
  filesystem_own_directory_entry_stream_t result;
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_read_directory(file_handle,
                                                        &result,
                                                        &error_code);
  if (!ok) {
    translate_error(&error_code);
    return NULL;
  }

  dirp->stream = result;
#elif defined(__wasip3__)
  filesystem_method_descriptor_read_directory(file_handle, &dirp->stream);
  dirp->stream_done = false;
#endif
  dirp->fd = fd;
  dirp->skip = 0;
  dirp->offset = 0;
  dirp->dirent = NULL;
  dirp->dirent_size = 1;
  DIR *ret = dirp;
  dirp = NULL;
  return ret;
#elif defined(__webp2__)
  (void)fd;
  WASILIBC_LOG_UNSUPPORTED("fdopendir");
  errno = ENOSYS;
  return NULL;
#else
# error "Unsupported WASI version"
#endif
}
