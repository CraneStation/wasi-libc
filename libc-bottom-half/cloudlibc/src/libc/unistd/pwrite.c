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
#include <common/errors.h>
#endif

#ifdef __wasip3__
#include <wasi/wasip3_block.h>
#endif

ssize_t pwrite(int fildes, const void *buf, size_t nbyte, off_t offset) {
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }

#if defined(__wasip1__)
  __wasi_ciovec_t iov = {.buf = buf, .buf_len = nbyte};
  size_t bytes_written;
  __wasi_errno_t error =
      __wasi_fd_pwrite(fildes, &iov, 1, offset, &bytes_written);
  if (error != 0) {
    __wasi_fdstat_t fds;
    if (error == ENOTCAPABLE && __wasi_fd_fdstat_get(fildes, &fds) == 0) {
      // Determine why we got ENOTCAPABLE.
      if ((fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) == 0)
        error = EBADF;
      else
        error = ESPIPE;
    }
    errno = error;
    return -1;
  }
  return bytes_written;
#elif defined(__wasip2__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Convert `buf` to a WASI byte list
  wasip2_list_u8_t contents;
  contents.len = nbyte;
  contents.ptr = (uint8_t*) buf;

  // Write the bytes
  filesystem_filesize_t bytes_written;
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_write(file_handle,
                                               &contents,
                                               offset,
                                               &bytes_written,
                                               &error_code);
  // Check for errors
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }

  return bytes_written;
#elif defined(__wasip3__)
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fildes, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Create a read/write stream, use `write-via-stream` to start writing,
  // then perform the write to see how much was accepted.
  filesystem_stream_u8_writer_t writer;
  filesystem_stream_u8_t reader = filesystem_stream_u8_new(&writer);
  filesystem_future_result_void_error_code_t result_future =
    filesystem_method_descriptor_write_via_stream(file_handle, reader, offset);
  bool closed;
  size_t ret = __wasilibc_stream_block_on(
    filesystem_stream_u8_write(writer, buf, nbyte),
    writer,
    &closed);
  filesystem_stream_u8_drop_writable(writer);

  // Wait for the future to resolve now that the writer half is closed and if
  // we failed to write bytes (0 bytes written) and the result is an error we
  // can return -1.
  filesystem_result_void_error_code_t result;
  __wasilibc_future_block_on(
      filesystem_future_result_void_error_code_read(result_future, &result),
      result_future);
  filesystem_future_result_void_error_code_drop_readable(result_future);
  if (ret == 0 && result.is_err) {
    translate_error(&result.val.err);
    return -1;
  }
  return ret;
#elif defined(__webp2__)
  (void)fildes;
  (void)buf;
  (void)nbyte;
  WASILIBC_LOG_UNSUPPORTED("pwrite");
  errno = ENOSYS;
  return -1;
#else
# error "Unsupported WASI version"
#endif
}
