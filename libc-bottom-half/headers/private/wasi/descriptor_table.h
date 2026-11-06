#ifndef DESCRIPTOR_TABLE_H
#define DESCRIPTOR_TABLE_H

#include <wasi/api.h>

#ifndef __wasip1__
#include <assert.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wasi/poll.h>

#ifdef __wasip3__

#include "lock.h"

/// The stream is complete and no further operations are allowed on it.
#define WASIP3_IO_DONE (1 << 0)
/// An I/O operation, be it a read or write, is in flight.
#define WASIP3_IO_INPROGRESS (1 << 1)
/// The in-flight I/O operation is a zero-length operation. This means that if
/// it's finished we expect that the next operation finishes immediately.
#define WASIP3_IO_ZERO_INPROGRESS (1 << 2)
/// A zero-sized in-flight I/O operation just finished so the next I/O op
/// should finish immediately. If it doesn't it'll turn on the next flag.
#define WASIP3_IO_SHOULD_BE_READY (1 << 3)
/// This stream isn't compatible with zero-length reads/writes signaling
/// readiness, so libc must buffer data internally for reads/writes.
#define WASIP3_IO_MUST_BUFFER (1 << 4)
/// A thread is actively blocked in an I/O operation and no other operation
/// can be performed.
#define WASIP3_IO_BLOCKED (1 << 5)

/// Helper structure to package up state related to a wasip3 `stream<u8>`.
///
/// This is used by various helpers to coordinate reading/writing/etc on a
/// stream. This simultaneously represents both readers and writers.
typedef struct wasip3_io_state_t {
  uint32_t stream;
  /// Bitset of `WASIP3_IO_*` flags.
  uint32_t flags;
  /// Malloc'd buffer used for reads/writes. NULL if not present.
  uint8_t *buf;
  /// Start of `buf` that has data in-flight or ready.
  size_t buf_start;
  /// End of `buf` that has data in-flight or ready.
  size_t buf_end;

#ifdef _REENTRANT
  /// Pointer to the containing object's lock, which protects all the state
  /// above. This is acquired and held through all I/O operations, but dropped
  /// while an operation that may indefinitely block is in flight.
  volatile int *lock;
  /// An internal lock used to hold a queue of threads waiting to do blocking
  /// I/O while some other thread is already doing blocking I/O.
  DECLARE_STRONG_LOCK(blocking_lock);
  /// Number of threads that are waiting on `blocking_lock` to take their turn
  /// doing blocking I/O. This count notably disallows a concurrent
  /// shutdown/reset/etc of this stream, for example `shutdown` fails while
  /// something is blocking and so does `seek`.
  size_t blocking_waiters;
#endif
} wasip3_io_state_t;

static inline void __wasip3_io_state_init(wasip3_io_state_t *state,
                                          uint32_t stream) {
  assert(stream != 0);
  memset(state, 0, sizeof(*state));
  state->stream = stream;
}

/// Initializes `state` with the `stream` provided, using `lock` — the
/// containing object's lock — to protect the state's fields.
#ifdef _REENTRANT
#define wasip3_io_state_init(state, stream, lock_ptr)                          \
  do {                                                                         \
    __wasip3_io_state_init(state, stream);                                     \
    (state)->lock = &(lock_ptr);                                               \
  } while (0)
#else
#define wasip3_io_state_init(state, stream, lock)                              \
  __wasip3_io_state_init(state, stream)
#endif

/// Tests whether `state` has been initialized with a stream yet.
static inline bool wasip3_io_state_present(wasip3_io_state_t *state) {
  return state->stream != 0;
}

/// Closes out the streams/etc internal to `state`.
///
/// Internally the stream must be a reader-half of a `stream<u8>`.
static inline void wasip3_read_state_close(wasip3_io_state_t *state) {
  STRONG_ASSERT_EMPTY(state->blocking_lock);
  if (state->flags & WASIP3_IO_INPROGRESS)
    filesystem_stream_u8_cancel_read(state->stream);
  if (state->buf)
    free(state->buf);
  if (state->stream != 0)
    filesystem_stream_u8_drop_readable(state->stream);
  memset(state, 0, sizeof(*state));
}

/// Closes out the streams/etc internal to `state`.
///
/// Internally the stream must be a writer-half of a `stream<u8>`.
///
/// Note that unlike `wasip3_read_state_close` this first blocks to flush any
/// buffered-but-unsent data, so this is a blocking operation even when the
/// descriptor is in nonblocking mode.
///
/// This requires `*state->lock` to be held by the caller, and that lock may be
/// dropped and reacquired while blocking.
#define wasip3_write_state_close __wasilibc_write_state_close
hidden void wasip3_write_state_close(wasip3_io_state_t *state);

/// Tests whether there is active or pending blocking I/O on this stream which
/// notably should prevent it from being closed.
static inline bool wasip3_io_state_blocked(wasip3_io_state_t *state) {
#ifdef _REENTRANT
  return (state->flags & WASIP3_IO_BLOCKED) || state->blocking_waiters > 0;
#else
  (void)state;
  return false;
#endif
}
#endif

// Metadata for WASI reads which is used to delegate to `__wasilibc_read(...)`
// to perform the actual read of a stream.
typedef struct wasi_read_t {
  // An optional pointer to the internal offset of this stream, updated on
  // successful reads.
  off_t *offset;
  // Whether or not this read will use blocking I/O.
  bool blocking;
  // The timeout, in nanoseconds, for this operation.
  monotonic_clock_duration_t timeout;
#ifdef __wasip2__
  // The `wasi:io/streams.input-stream` that this is reading from.
  streams_borrow_input_stream_t input;
  // A required pointer to an owned pollable for `input`. This is lazily
  // initialized as-necessary.
  poll_own_pollable_t *pollable;
#else
  wasip3_io_state_t *state;
#endif

  // A callback/ptr pair to invoke when EOF is reached to set errno and return
  // an error code. This is optional on wasip2, where a read at EOF otherwise
  // returns 0.
  int (*eof)(void *);
  void *eof_data;
} wasi_read_t;

// Same as `wasip_read_t`, but for writes.
typedef struct wasi_write_t {
  off_t *offset;
  bool blocking;
  monotonic_clock_duration_t timeout;

#ifdef __wasip2__
  streams_borrow_output_stream_t output;
  poll_own_pollable_t *pollable;
#else
  wasip3_io_state_t *state;
  // A callback/ptr pair to invoke when EOF is reached to set errno and return
  // an error code.
  int (*eof)(void *);
  void *eof_data;
#endif
} wasi_write_t;

/**
 * Operations that are required of all descriptors registered as file
 * descriptors.
 *
 * This structure is used to dynamically dispatch operations various I/O
 * objects like files and sockets. Functions pointers below can all be `NULL`
 * except for `free`. These are the implementation of the corresponding
 * functions in libc.
 *
 * All functions below communicate errors through the return value. A -1 return
 * value indicates that an error happened and that `errno` is set.
 */
typedef struct descriptor_vtable_t {
  /// Deallocates the parameter provided, closing all resources as well.
  void (*free)(void *);

  // =====================================================================
  // Generic I/O

  /// Looks up metadata to perform a read operation for this stream. This is
  /// used to implement the `read` syscall, for example, and is also used with
  /// `poll` when waiting for readability.
  ///
  /// When threading is enabled for WASIp3+ this additionally acquires the
  /// internal lock for the object and the caller must unlock the
  /// `read->state->lock` object on success. On failure no locks are held when
  /// this returns.
  int (*get_read_stream)(void *, wasi_read_t *);
  /// Same as `get_read_stream`, but for output streams.
  int (*get_write_stream)(void *, wasi_write_t *);

  /// Sets the nonblocking flag for this object to the specified value.
  int (*set_blocking)(void *, bool);

  /// Implementation of `fstat` the function call, used to learn about file
  /// descriptors.
  int (*fstat)(void *, struct stat *);

  // =====================================================================
  // File-related APIs

  /// Looks up a `wasi:filesystem/types.descriptor`, if present, from this
  /// object.
  int (*get_file)(void *, filesystem_borrow_descriptor_t *);
  /// Implementation of `lseek`-the-function.
  off_t (*seek)(void *, off_t, int);
  /// Implementation of `fnctl(fd, F_GETFL)`.
  int (*fcntl_getfl)(void *);
  /// Implementation of `fnctl(fd, F_SETFL)`.
  int (*fcntl_setfl)(void *, int);
  /// Implementation of `isatty`-the-function.
  int (*isatty)(void *);

  // =====================================================================
  // Sockets-related APIs

  /// Implementation of `accept4`-the-function.
  int (*accept4)(void *, struct sockaddr *addr, socklen_t *addrlen, int flags);
  /// Implementation of `bind`-the-function.
  int (*bind)(void *, const struct sockaddr *addr, socklen_t addrlen);
  /// Implementation of `connect`-the-function.
  int (*connect)(void *, const struct sockaddr *addr, socklen_t addrlen);
  /// Implementation of `getsockname`-the-function.
  int (*getsockname)(void *, struct sockaddr *addr, socklen_t *addrlen);
  /// Implementation of `getpeername`-the-function.
  int (*getpeername)(void *, struct sockaddr *addr, socklen_t *addrlen);
  /// Implementation of `listen`-the-function.
  int (*listen)(void *, int backlog);
  /// Implementation of `recvfrom`-the-function.
  ssize_t (*recvfrom)(void *, void *buffer, size_t length, int flags,
                      struct sockaddr *addr, socklen_t *addrlen);
  /// Implementation of `sendto`-the-function.
  ssize_t (*sendto)(void *, const void *buffer, size_t length, int flags,
                    const struct sockaddr *addr, socklen_t addrlen);
  /// Implementation of `shutdown`-the-function.
  int (*shutdown)(void *, int how);
  /// Implementation of `getsockopt`-the-function.
  int (*getsockopt)(void *, int level, int optname, void *optval,
                    socklen_t *optlen);
  /// Implementation of `setsockopt`-the-function.
  int (*setsockopt)(void *, int level, int optname, const void *optval,
                    socklen_t optlen);

  // =====================================================================
  // `poll`-related APIs

  /// Invoked when this descriptor is passed as one of the objects to the
  /// `poll` function. The `wasi/poll.h` header has functions that can operate
  /// on the `state` provided. This should invoke `__wasilibc_poll_add`, for
  /// example, for any event in `events` with a pollable derived from this
  /// object.
  ///
  /// If this function is not provided then `poll` will use `get_read_stream`
  /// and `get_write_stream`, if present, to handle `POLL{RD,WR}NORM` events.
  int (*poll_register)(void *, poll_state_t *state, short events);

#ifdef __wasip2__
  /// Invoked when `poll` has already run and detected that this object was
  /// ready. The `events` provided are the same as those provided to
  /// `__wasilibc_poll_add` originally. This function should call
  /// `__wasilibc_poll_ready` for example to indicate that the underlying
  /// `pollfd` structure has some events ready.
  ///
  /// If this function is not provided then `events` will automatically
  /// be placed into the `revents` field of `pollfd`.
  int (*poll_finish)(void *, poll_state_t *state, short events);
#endif // __wasip2__
} descriptor_vtable_t;

/// Reference count structure that must be at the beginning of all data passed
/// to `descriptor_table_insert`.
typedef struct {
  unsigned cnt;
} descriptor_refcnt_t;

/// A "fat pointer" which is placed inside of the descriptor table.
typedef struct {
  /// Arbitrary descriptor-specific data passed to `vtable` function pointer.
  descriptor_refcnt_t *data;
  /// Definition of various operations for this descriptor.
  descriptor_vtable_t *vtable;
} descriptor_table_entry_t;

/// Inserts the `entry` provided into the descriptor table, returning the
/// integer file descriptor used to refer to it.
///
/// Note that `entry` is expected to have a reference count of 0 (freshly
/// initialized), and it'll get initialized within this function.
///
/// On failure returns -1, sets `errno`, and runs `entry`'s destructor.
hidden int descriptor_table_insert(descriptor_table_entry_t entry);

/// Operations that `descriptor_table_dup` supports.
enum dup_op_t {
  /// `dup` function, no `arg`
  DUP_OP_DUP,
  /// `dup2` function, `arg` is specified
  DUP_OP_DUP2,
  /// `dup3` function, `arg` is specified
  DUP_OP_DUP3,
  /// `F_DUPFD` fcntl, `arg` is specified
  DUP_OP_DUPFD,
};

/// Performs a `dup`-style operation.
hidden int descriptor_table_dup(int fd, enum dup_op_t op, int arg);

/// Looks up a descriptor by its file descriptor.
///
/// On success returns 0 and `entry` is filled in with a strong reference to
/// the new entry. Callers must call `descriptor_table_entry_dec` when they're
/// done with the entry. On failure -1 is returned an `errno` is set.
hidden int descriptor_table_get(int fd, descriptor_table_entry_t *entry);

/// Removes the specified file descriptor from the table.
///
/// On success returns 0 and runs the entry's destructor. On failure returns -1
/// and sets `errno`.
hidden int descriptor_table_remove(int fd);

/// Removes all file descriptors from the table, running their destructors.
hidden void descriptor_table_clear();

/// Increment the reference count of the `entry` provided.
static inline void descriptor_table_entry_inc(descriptor_table_entry_t entry) {
  assert(entry.data->cnt > 0);
  entry.data->cnt++;
}

/// Slow-path deallocation routine of `descriptor_table_entry_dec`.
///
/// Assumes refcount is already 0.
hidden void __wasilibc_descriptor_deallocate(descriptor_table_entry_t entry);

/// Decrement the reference count of the `entry` provided, running deallocation
/// if the cnt reaches 0.
static inline void descriptor_table_entry_dec(descriptor_table_entry_t entry) {
  assert(entry.data->cnt > 0);
  entry.data->cnt--;
  if (entry.data->cnt == 0)
    __wasilibc_descriptor_deallocate(entry);
}

// Helper function used at program-exit time to assert that there are no
// descriptor leaks when libc is itself built in debug mode. Note that this is
// a noop when compiled with `-DNDEBUG`.
#ifdef NDEBUG
static inline void __wasilibc_assert_no_descriptor_leaks(void) {}
#else
void __wasilibc_assert_no_descriptor_leaks(void);
#endif

#endif // __wasip1__

#endif // DESCRIPTOR_TABLE_H
