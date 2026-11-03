#ifndef __wasi_file_utils_h
#define __wasi_file_utils_h

#include <wasi/version.h>

#if defined(__wasip2__) || defined(__wasip3__)

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <wasi/descriptor_table.h>
#include <wasi/poll.h>
#include <wasi/wasip2.h>

#ifdef __wasip2__
// Sets errno for a failed stream operation and releases its error resource.
void __wasilibc_set_stream_errno(streams_own_error_t error);

/// Handles a `wasi:io/streams.stream-error` for a `read`-style operation.
///
/// If the error indicates "closed" then 0 is returned to mean EOF. Otherwise
/// the last-operation-failed handle is closed and errno is set to the
/// corresponding filesystem errno when available, else EIO.
static inline int wasip2_handle_read_error(streams_stream_error_t error) {
  if (error.tag == STREAMS_STREAM_ERROR_CLOSED) {
    return 0;
  }
  assert(error.tag == STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED);
  __wasilibc_set_stream_errno(error.val.last_operation_failed);
  return -1;
}

/// Same as `wasip2_handle_read_error` except "closed" now returns EPIPE
/// instead of 0.
static inline int wasip2_handle_write_error(streams_stream_error_t error) {
  if (error.tag == STREAMS_STREAM_ERROR_CLOSED) {
    errno = EPIPE;
    return -1;
  }
  assert(error.tag == STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED);
  __wasilibc_set_stream_errno(error.val.last_operation_failed);
  return -1;
}

/// Blocks the current thread until `pollable` is ready, waiting at most
/// `timeout` nanoseconds for that to happen.
///
/// If `timeout` is 0 then this waits indefinitely. Returns `0` if `pollable`
/// became ready or `-1` with `errno` set if the timeout elapsed first.
int __wasilibc_pollable_block_on(poll_borrow_pollable_t pollable,
                                 monotonic_clock_duration_t timeout);
#endif

#ifdef __wasip2__
typedef wasip2_string_t wasi_string_t;
#else
typedef wasip3_string_t wasi_string_t;
#endif

// Converts the C string `s` into a WASI string stored in `out`.
//
// The returned `wasip{2,3}_string_t` should not be deallocated or free'd, and
// it can only be used while `s` is also valid.
//
// Returns 0 if `s` is valid utf-8.
// Returns -1 and sets errno to `ENOENT` if `s` is not valid utf-8.
int wasi_string_from_c(const char *s, wasi_string_t *out);

// Looks up the `fd` specified and acquires the WASI file handle within it,
// storing it into `result`.
//
// On success 0 is returned and `entry` and `result` are filled in. Callers must
// use `descriptor_table_entry_dec` on `entry`.
//
// On failure -1, errno is set, and nothing else need be done.
static inline int fd_to_file_handle(int fd, descriptor_table_entry_t *entry,
                                    filesystem_borrow_descriptor_t *result) {
  if (descriptor_table_get(fd, entry) < 0)
    return -1;
  if (!entry->vtable->get_file) {
    descriptor_table_entry_dec(*entry);
    errno = EINVAL;
    return -1;
  }
  if (entry->vtable->get_file(entry->data, result) < 0) {
    descriptor_table_entry_dec(*entry);
    return -1;
  }
  return 0;
}

// Reads from `read` into `buf`/`len`
//
// This perform the read configured by `read`, e.g. whether it's blocking or
// not, and places the result in the specified buffer. Used to implement
// `read` and `recvfrom`, for example.
ssize_t __wasilibc_read(wasi_read_t *read, void *buf, size_t len);
// Same as `__wasilibc_read`, but for writes.
ssize_t __wasilibc_write(wasi_write_t *write, const void *buf, size_t len);

static inline unsigned
dir_entry_type_to_d_type(filesystem_descriptor_type_t *ty) {
#ifdef __wasip2__
  switch (*ty) {
#else
  switch (ty->tag) {
#endif

#ifdef __wasip2__
  case FILESYSTEM_DESCRIPTOR_TYPE_UNKNOWN:
    return DT_UNKNOWN;
#endif

#ifdef __wasip3__
  case FILESYSTEM_DESCRIPTOR_TYPE_OTHER:
    if (ty->val.other.is_some) {
      assert(ty->val.other.val.ptr);
      wasip3_string_free(&ty->val.other.val);
      ty->val.other.is_some = false;
    }
    return DT_UNKNOWN;
#endif

  case FILESYSTEM_DESCRIPTOR_TYPE_BLOCK_DEVICE:
    return DT_BLK;
  case FILESYSTEM_DESCRIPTOR_TYPE_CHARACTER_DEVICE:
    return DT_CHR;
  case FILESYSTEM_DESCRIPTOR_TYPE_DIRECTORY:
    return DT_DIR;
  case FILESYSTEM_DESCRIPTOR_TYPE_FIFO:
    return DT_FIFO;
  case FILESYSTEM_DESCRIPTOR_TYPE_SYMBOLIC_LINK:
    return DT_LNK;
  case FILESYSTEM_DESCRIPTOR_TYPE_REGULAR_FILE:
    return DT_REG;
  case FILESYSTEM_DESCRIPTOR_TYPE_SOCKET:
    return DT_SOCK;
  default:
    return DT_UNKNOWN;
  }
}

#ifndef __wasip2__
/// Starts the process of `poll` for `iostate` assuming it's backed by a
/// readable stream.
///
/// This will handle all the internal logic for zero-length reads/writes/etc
/// and add to `state` as necessary.
int __wasilibc_read_poll(wasip3_io_state_t *iostate, poll_state_t *state);

/// Write dual of `__wasilibc_read_poll`.
int __wasilibc_write_poll(wasip3_io_state_t *iostate, poll_state_t *state);
#endif // !__wasip2__

#endif // __wasip2__ || __wasip3__

#endif // __wasi_file_utils_h
