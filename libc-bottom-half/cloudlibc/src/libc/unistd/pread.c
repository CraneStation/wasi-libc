// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <string.h>
#include <common/errors.h>
#endif

#ifdef __wasip3__
#include <wasi/wasip3_block.h>
#endif

ssize_t pread(int fildes, void *buf, size_t nbyte, off_t offset) {
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }

#if defined(__wasip1__)
  __wasi_iovec_t iov = {.buf = buf, .buf_len = nbyte};
  size_t bytes_read;
  __wasi_errno_t error =
      __wasi_fd_pread(fildes, &iov, 1, offset, &bytes_read);
  if (error != 0) {
    __wasi_fdstat_t fds;
    if (error == ENOTCAPABLE && __wasi_fd_fdstat_get(fildes, &fds) == 0) {
      // Determine why we got ENOTCAPABLE.
      if ((fds.fs_rights_base & __WASI_RIGHTS_FD_READ) == 0)
        error = EBADF;
      else
        error = ESPIPE;
    }
    errno = error;
    return -1;
  }
  return bytes_read;
#elif defined(__wasip2__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Set up a WASI tuple to receive the results
  wasip2_tuple2_list_u8_bool_t contents;

  // Read the bytes
  filesystem_error_code_t error_code;
  size_t bytes_read;
  bool ok = filesystem_method_descriptor_read(file_handle,
                                              nbyte,
                                              offset,
                                              &contents,
                                              &error_code);
  // Check for errors
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }

  bytes_read = contents.f0.len;
  // Copy the bytes allocated in the canonical ABI to `buf`
  memcpy(buf, contents.f0.ptr, bytes_read);
  wasip2_list_u8_free(&contents.f0);

  return bytes_read;
#elif defined(__wasip3__)
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Use `read-via-stream` to acquire a stream of data at this specified file
  // offset, then issue a read and wait for it to finish.
  filesystem_tuple2_stream_u8_future_result_void_error_code_t io;
  filesystem_method_descriptor_read_via_stream(file_handle, offset, &io);
  bool closed;
  size_t ret = __wasilibc_stream_block_on(
    filesystem_stream_u8_read(io.f0, buf, nbyte),
    io.f0,
    &closed);
  filesystem_stream_u8_drop_readable(io.f0);

  // If zero bytes were read then read the provided future to see
  // what happened. Otherwise close the future as we won't be reading the
  // result.
  if (ret == 0) {
    filesystem_result_void_error_code_t result;
    __wasilibc_future_block_on(
      filesystem_future_result_void_error_code_read(io.f1, &result),
      io.f1);
    filesystem_future_result_void_error_code_drop_readable(io.f1);
    if (result.is_err) {
      translate_error(&result.val.err);
      return -1;
    }
  } else {
    filesystem_future_result_void_error_code_drop_readable(io.f1);
  }
  return ret;
#elif defined(__webp2__)
  (void)fildes;
  (void)buf;
  (void)nbyte;
  WASILIBC_LOG_UNSUPPORTED("pread");
  errno = ENOSYS;
  return -1;
#else
# error "Unknown WASI version"
#endif
}

