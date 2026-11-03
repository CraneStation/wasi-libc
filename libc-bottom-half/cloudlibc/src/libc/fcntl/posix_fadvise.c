// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <wasi/api.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#ifdef __wasip1__
static_assert(POSIX_FADV_DONTNEED == __WASI_ADVICE_DONTNEED,
              "Value mismatch");
static_assert(POSIX_FADV_NOREUSE == __WASI_ADVICE_NOREUSE, "Value mismatch");
static_assert(POSIX_FADV_NORMAL == __WASI_ADVICE_NORMAL, "Value mismatch");
static_assert(POSIX_FADV_RANDOM == __WASI_ADVICE_RANDOM, "Value mismatch");
static_assert(POSIX_FADV_SEQUENTIAL == __WASI_ADVICE_SEQUENTIAL,
              "Value mismatch");
static_assert(POSIX_FADV_WILLNEED == __WASI_ADVICE_WILLNEED,
              "Value mismatch");
#endif

int posix_fadvise(int fd, off_t offset, off_t len, int advice) {
  if (offset < 0 || len < 0)
    return EINVAL;

#if defined(__wasip1__)
  return __wasi_fd_advise(fd, offset, len, advice);
#elif defined(__wasip2__) || defined(__wasip3__)
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return EBADF;
  defer descriptor_table_entry_dec(entry);
  if (file_handle.__handle == 0) {
    errno = EBADF;
    return EBADF;
  }

  filesystem_error_code_t error_code;
  filesystem_advice_t fs_advice;
  switch (advice) {
  case POSIX_FADV_NORMAL:
    fs_advice = FILESYSTEM_ADVICE_NORMAL;
    break;
  case POSIX_FADV_SEQUENTIAL:
    fs_advice = FILESYSTEM_ADVICE_SEQUENTIAL;
    break;
  case POSIX_FADV_RANDOM:
    fs_advice = FILESYSTEM_ADVICE_RANDOM;
    break;
  case POSIX_FADV_NOREUSE:
    fs_advice = FILESYSTEM_ADVICE_NO_REUSE;
    break;
  case POSIX_FADV_WILLNEED:
    fs_advice = FILESYSTEM_ADVICE_WILL_NEED;
    break;
  case POSIX_FADV_DONTNEED:
    fs_advice = FILESYSTEM_ADVICE_DONT_NEED;
    break;
  default: {
    errno = EINVAL;
    return EINVAL;
  }
  }
  bool ok = filesystem_method_descriptor_advise(file_handle, offset, len, fs_advice, &error_code);
  if (!ok) {
    translate_error(&error_code);
    return errno;
  }
  return 0;
#elif defined(__webp2__)
  (void)fd;
  (void)advice;
  return 0;
#else
# error "Unsupported WASI version"
#endif
}
