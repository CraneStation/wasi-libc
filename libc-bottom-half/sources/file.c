#include "libc/sys/stat/stat_impl.h"
#include "lock.h"
#include <assert.h>
#include <common/errors.h>
#include <errno.h>
#include <fcntl.h>
#include <stddefer.h>
#include <wasi/descriptor_table.h>
#include <wasi/file.h>
#include <wasi/wasip2.h>
#include <wasi/wasip3_block.h>

typedef struct {
  descriptor_refcnt_t refcnt;
  DECLARE_STRONG_LOCK(lock);
  filesystem_own_descriptor_t file_handle;
  // Current position in stream, relative to the beginning of the
  // *file_handle*, measured in bytes
  off_t offset;
  int oflag;
#ifdef __wasip2__
  // Lazily initialized read/write streams which track the current position
  // in the file. Seeking will close these streams and cause them to be
  // reopened on the next operation, for example, and reading to the end of
  // the file closes the read stream.
  streams_own_input_stream_t read_stream;
  streams_own_output_stream_t write_stream;
  // Used for checking readiness to read/write to stream. Lazily initialized
  // and initially set to 0.
  streams_own_pollable_t read_pollable;
  streams_own_pollable_t write_pollable;
#else
  wasip3_io_state_t read;
  filesystem_future_result_void_error_code_t read_result;
  wasip3_io_state_t write;
  filesystem_future_result_void_error_code_t write_result;
#endif
} file_t;

static void file_close_streams(void *data) {
  file_t *file = (file_t *)data;
  STRONG_ASSERT_HELD(file->lock);

#ifdef __wasip2__
  if (file->read_pollable.__handle != 0) {
    poll_pollable_drop_own(file->read_pollable);
    file->read_pollable.__handle = 0;
  }
  if (file->write_pollable.__handle != 0) {
    poll_pollable_drop_own(file->write_pollable);
    file->write_pollable.__handle = 0;
  }
  if (file->read_stream.__handle != 0) {
    streams_input_stream_drop_own(file->read_stream);
    file->read_stream.__handle = 0;
  }
  if (file->write_stream.__handle != 0) {
    streams_output_stream_drop_own(file->write_stream);
    file->write_stream.__handle = 0;
  }
#else
  wasip3_read_state_close(&file->read);
  wasip3_write_state_close(&file->write);
  if (file->read_result != 0) {
    filesystem_future_result_void_error_code_drop_readable(file->read_result);
    file->read_result = 0;
  }
  if (file->write_result != 0) {
    filesystem_future_result_void_error_code_drop_readable(file->write_result);
    file->write_result = 0;
  }
#endif
}

static void file_free(void *data) {
  file_t *file = (file_t *)data;
  STRONG_ASSERT_EMPTY(file->lock);
  STRONG_LOCK(file->lock);
  file_close_streams(data);
  STRONG_UNLOCK(file->lock);
  filesystem_descriptor_drop_own(file->file_handle);
  free(file);
}

#ifdef __wasip2__
static int file_read_eof(void *data) {
  file_t *file = (file_t *)data;
  STRONG_ASSERT_HELD(file->lock);

  // An input stream stays closed once it reaches the end of the file, even if
  // the file grows afterwards, so drop it and let the next read open a new
  // stream at the same offset.
  if (file->read_pollable.__handle != 0) {
    poll_pollable_drop_own(file->read_pollable);
    file->read_pollable.__handle = 0;
  }
  streams_input_stream_drop_own(file->read_stream);
  file->read_stream.__handle = 0;
  return 0;
}
#else
static int file_read_eof(void *data) {
  file_t *file = (file_t *)data;
  STRONG_ASSERT_HELD(file->lock);

  int ret = 0;
  if (file->read_result != 0) {
    filesystem_result_void_error_code_t result;
    __wasilibc_future_block_on(filesystem_future_result_void_error_code_read(
                                   file->read_result, &result),
                               file->read_result);
    filesystem_future_result_void_error_code_drop_readable(file->read_result);
    file->read_result = 0;
    if (result.is_err) {
      translate_error(&result.val.err);
      ret = -1;
    }
  }

  // As on wasip2, a stream that reached the end of the file doesn't see data
  // appended afterwards, so the next read opens a new one, unless another
  // thread is still waiting to read from this one.
  if (!wasip3_io_state_blocked(&file->read))
    wasip3_read_state_close(&file->read);
  return ret;
}
#endif

static int file_get_read_stream(void *data, wasi_read_t *read) {
  file_t *file = (file_t *)data;
  STRONG_LOCK(file->lock);
  // .. intentionally don't unlock `file->lock` as this function lets the
  // caller do that (see `descriptor_table.h` for details of this callback).

#ifdef __wasip2__
  if (file->read_stream.__handle == 0) {
    filesystem_error_code_t error_code;
    bool ok = filesystem_method_descriptor_read_via_stream(
        filesystem_borrow_descriptor(file->file_handle), file->offset,
        &file->read_stream, &error_code);
    if (!ok) {
      translate_error(&error_code);
      return -1;
    }
  }
  read->input = streams_borrow_input_stream(file->read_stream);
  read->pollable = &file->read_pollable;
#else
  if (!wasip3_io_state_present(&file->read)) {
    assert(!file->read_result);
    filesystem_tuple2_stream_u8_future_result_void_error_code_t result;
    filesystem_method_descriptor_read_via_stream(
        filesystem_borrow_descriptor(file->file_handle), file->offset, &result);
    wasip3_io_state_init(&file->read, result.f0, file->lock);
    file->read_result = result.f1;
  }
  read->state = &file->read;
#endif
  read->eof = file_read_eof;
  read->eof_data = data;
  read->offset = &file->offset;
  read->timeout = 0;
  read->blocking = true;
  return 0;
}

#ifndef __wasip2__
static int file_write_eof(void *data) {
  file_t *file = (file_t *)data;
  STRONG_ASSERT_HELD(file->lock);

  if (file->write_result) {
    filesystem_result_void_error_code_t result;
    __wasilibc_future_block_on(filesystem_future_result_void_error_code_read(
                                   file->write_result, &result),
                               file->write_result);
    filesystem_future_result_void_error_code_drop_readable(file->write_result);
    file->write_result = 0;

    if (result.is_err) {
      translate_error(&result.val.err);
      return -1;
    }
  }

  return 0;
}
#endif

static int file_get_write_stream(void *data, wasi_write_t *write) {
  file_t *file = (file_t *)data;
  STRONG_LOCK(file->lock);
  // .. intentionally don't unlock `file->lock` as this function lets the
  // caller do that (see `descriptor_table.h` for details of this callback).

#ifdef __wasip2__
  if (file->write_stream.__handle == 0) {
    filesystem_error_code_t error_code;
    bool ok;

    if (file->oflag & O_APPEND) {
      ok = filesystem_method_descriptor_append_via_stream(
          filesystem_borrow_descriptor(file->file_handle), &file->write_stream,
          &error_code);
    } else {
      ok = filesystem_method_descriptor_write_via_stream(
          filesystem_borrow_descriptor(file->file_handle), file->offset,
          &file->write_stream, &error_code);
    }
    if (!ok) {
      translate_error(&error_code);
      return -1;
    }
  }
  write->output = streams_borrow_output_stream(file->write_stream);
  write->pollable = &file->write_pollable;
#else
  if (!wasip3_io_state_present(&file->write)) {
    assert(file->write_result == 0);
    filesystem_stream_u8_writer_t writer;
    filesystem_stream_u8_t write_read = filesystem_stream_u8_new(&writer);
    if (file->oflag & O_APPEND) {
      file->write_result = filesystem_method_descriptor_append_via_stream(
          filesystem_borrow_descriptor(file->file_handle), write_read);
    } else {
      file->write_result = filesystem_method_descriptor_write_via_stream(
          filesystem_borrow_descriptor(file->file_handle), write_read,
          file->offset);
    }
    wasip3_io_state_init(&file->write, writer, file->lock);
  }
  write->state = &file->write;
  write->eof = file_write_eof;
  write->eof_data = data;
#endif
  write->offset = &file->offset;
  write->timeout = 0;
  write->blocking = true;
  return 0;
}

static int file_get_file(void *data, filesystem_borrow_descriptor_t *out_file) {
  file_t *file = (file_t *)data;
  *out_file = filesystem_borrow_descriptor(file->file_handle);
  return 0;
}

static int file_fstat(void *data, struct stat *buf) {
  file_t *file = (file_t *)data;
  // Get the metadata hash for the file descriptor
  filesystem_metadata_hash_value_t metadata;
  filesystem_error_code_t error;
  if (!filesystem_method_descriptor_metadata_hash(
          filesystem_borrow_descriptor(file->file_handle), &metadata, &error)) {
    translate_error(&error);
    return -1;
  }

  // Get the file metadata
  filesystem_descriptor_stat_t internal_stat;
  bool stat_result = filesystem_method_descriptor_stat(
      filesystem_borrow_descriptor(file->file_handle), &internal_stat, &error);
  if (!stat_result) {
    translate_error(&error);
    return -1;
  }

  // Convert the internal data to an external struct
  to_public_stat(&metadata, &internal_stat, buf);
  return 0;
}

static int file_seek_end(file_t *file) {
  STRONG_ASSERT_HELD(file->lock);
  filesystem_descriptor_stat_t stat;
  filesystem_error_code_t error;
  bool ok = filesystem_method_descriptor_stat(
      filesystem_borrow_descriptor(file->file_handle), &stat, &error);
  if (!ok) {
    translate_error(&error);
    return -1;
  }
  file->offset = (off_t)stat.size;
  return 0;
}

static off_t file_seek(void *data, off_t offset, int whence) {
  file_t *file = (file_t *)data;

  STRONG_LOCK(file->lock);
  defer STRONG_UNLOCK(file->lock);

#ifndef __wasip2__
  // If another thread is blocked in an I/O operation on this file then
  // disallow this concurrent operation: `file_close_streams` below can't
  // cancel a blocking operation in another thread, and supporting the seek
  // would additionally require synchronizing updates to `offset` which
  // currently isn't done.
  if (wasip3_io_state_blocked(&file->read) ||
      wasip3_io_state_blocked(&file->write)) {
    errno = EOPNOTSUPP;
    return -1;
  }
#endif

  // If this file is in append mode, reset our knowledge of the current cursor
  // to the current end of the file.
  if ((file->oflag & O_APPEND) && file_seek_end(file) < 0)
    return -1;

  off_t result;
  switch (whence) {
  case SEEK_SET:
    result = offset;
    break;
  case SEEK_CUR:
    result = file->offset + offset;
    break;
  case SEEK_END: {
    // If we're in append mode we already reset to the end, but if we're not
    // in append mode then do the reset to the end here.
    if (!(file->oflag & O_APPEND) && file_seek_end(file) < 0)
      return -1;
    result = file->offset + offset;
    break;
  }
  default:
    errno = EINVAL;
    return -1;
  }
  if (result < 0) {
    errno = EINVAL;
    return -1;
  }
  if (result != file->offset) {
    file_close_streams(data);
    file->offset = result;
  }
  return file->offset;
}

static int file_set_blocking(void *data, bool blocking) {
  file_t *file = (file_t *)data;
  STRONG_LOCK(file->lock);
  defer STRONG_UNLOCK(file->lock);

  if (blocking)
    file->oflag &= ~O_NONBLOCK;
  else
    file->oflag |= O_NONBLOCK;
  return 0;
}

static int file_fcntl_getfl(void *data) {
  file_t *file = (file_t *)data;
  STRONG_LOCK(file->lock);
  defer STRONG_UNLOCK(file->lock);

  // Get the flags of the descriptor
  filesystem_descriptor_flags_t flags;
  filesystem_error_code_t error_code;

  filesystem_borrow_descriptor_t file_handle =
      filesystem_borrow_descriptor(file->file_handle);
  if (!filesystem_method_descriptor_get_flags(file_handle, &flags,
                                              &error_code)) {
    translate_error(&error_code);
    return -1;
  }

  int oflags = file->oflag & (O_NONBLOCK | O_APPEND);
  if (flags & FILESYSTEM_DESCRIPTOR_FLAGS_READ) {
    if (flags & FILESYSTEM_DESCRIPTOR_FLAGS_WRITE)
      oflags |= O_RDWR;
    else
      oflags |= O_RDONLY;
  } else if (flags & FILESYSTEM_DESCRIPTOR_FLAGS_WRITE) {
    oflags |= O_WRONLY;
  } else {
    oflags |= O_SEARCH;
  }
  return oflags;
}

static int file_fcntl_setfl(void *data, int flags) {
  file_t *file = (file_t *)data;
  STRONG_LOCK(file->lock);
  defer STRONG_UNLOCK(file->lock);

  flags &= O_NONBLOCK | O_APPEND;
  file->oflag = (file->oflag & ~(O_NONBLOCK | O_APPEND)) | flags;
  return 0;
}

static descriptor_vtable_t file_vtable = {
    .free = file_free,
    .get_read_stream = file_get_read_stream,
    .get_write_stream = file_get_write_stream,
    .get_file = file_get_file,
    .set_blocking = file_set_blocking,
    .fstat = file_fstat,
    .seek = file_seek,
    .fcntl_getfl = file_fcntl_getfl,
    .fcntl_setfl = file_fcntl_setfl,
};

int __wasilibc_add_file(filesystem_own_descriptor_t file_handle, int oflag) {
  file_t *file = calloc(1, sizeof(file_t));
  if (!file) {
    filesystem_descriptor_drop_own(file_handle);
    errno = ENOMEM;
    return -1;
  }
  assert(file_handle.__handle != 0);
  file->file_handle = file_handle;
  file->oflag = oflag;

  descriptor_table_entry_t entry;
  entry.vtable = &file_vtable;
  entry.data = &file->refcnt;
  return descriptor_table_insert(entry);
}
