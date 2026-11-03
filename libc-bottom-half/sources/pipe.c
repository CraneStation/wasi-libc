#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <wasi/report-error.h>

#if defined(__wasip3__)

#include <limits.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>
#include <wasi/wasip3_block.h>

// Manual bindings to a `stream` in the component model (no payload) where the
// name mangling is what's recognized by `wit-component` at this time.
__attribute__((import_module("$root"),
               import_name("[stream-new-unit]"))) extern uint64_t
stream_new_unit(void);
__attribute__((import_module("$root"),
               import_name("[stream-drop-readable-unit]"))) extern void
stream_drop_readable_unit(uint32_t);
__attribute__((import_module("$root"),
               import_name("[stream-drop-writable-unit]"))) extern void
stream_drop_writable_unit(uint32_t);
__attribute__((import_module("$root"),
               import_name("[stream-cancel-read-unit]"))) extern uint32_t
stream_cancel_read_unit(uint32_t);
__attribute__((import_module("$root"),
               import_name("[stream-cancel-write-unit]"))) extern uint32_t
stream_cancel_write_unit(uint32_t);
__attribute__((import_module("$root"),
               import_name("[async-lower][stream-write-unit]"))) extern uint32_t
stream_write_unit(uint32_t stream, const void *ptr, size_t len);
__attribute__((import_module("$root"),
               import_name("[async-lower][stream-read-unit]"))) extern uint32_t
stream_read_unit(uint32_t stream, void *ptr, size_t len);

/// The reader-half of a pipe is still alive.
#define PIPE_READER_ALIVE (1 << 0)
/// The writer-half of a pipe is still alive.
#define PIPE_WRITER_ALIVE (1 << 1)
/// The pipe has a reader that is currently blocked.
#define PIPE_READER_BLOCKED (1 << 2)
/// The pipe has a writer that is currently blocked.
#define PIPE_WRITER_BLOCKED (1 << 3)
/// The pipe has delivered a notification to a blocked reader.
#define PIPE_READER_WOKEN (1 << 4)
/// The pipe has delivered a notification to a blocked writer.
#define PIPE_WRITER_WOKEN (1 << 5)

typedef struct {
  DECLARE_STRONG_LOCK(lock);

  /// Bitset of `PIPE_*` above.
  int flags;

  /// Internal buffer managed by this pipe. Currently this is never resized and
  /// only lazily allocated once.
  void *buffer;
  size_t buffer_len;
  size_t buffer_cap;

  /// Lazily allocated component-model stream handles.
  uint32_t stream_reader;
  uint32_t stream_writer;
} pipe_state_t;

typedef struct {
  descriptor_refcnt_t cnt;
  pipe_state_t *state;
  /// Whether or not this is the read-half of the pipe.
  bool reader;
  /// Whether this half is in blocking mode. The two halves of a pipe are
  /// distinct open file descriptions, so this is tracked per-half rather than
  /// in the shared state. Protected by `state->lock`.
  bool blocking;
} pipe_t;

static void pipe_state_free(pipe_state_t *state) {
  STRONG_ASSERT_EMPTY(state->lock);
  assert(!(state->flags & PIPE_READER_ALIVE));
  assert(!(state->flags & PIPE_WRITER_ALIVE));
  if (state->stream_reader)
    stream_drop_readable_unit(state->stream_reader);
  if (state->stream_writer)
    stream_drop_writable_unit(state->stream_writer);
  free(state->buffer);
  free(state);
}

/// Lazily allocate the component model stream for this pipe if it's not already
/// allocated.
static void pipe_ensure_stream(pipe_t *pipe) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  if (pipe->state->stream_reader)
    return;

  uint64_t stream = stream_new_unit();
  pipe->state->stream_reader = (uint32_t)stream;
  pipe->state->stream_writer = (uint32_t)(stream >> 32);
  assert(pipe->state->stream_reader);
  assert(pipe->state->stream_writer);
}

static void pipe_unblock_writer(pipe_t *pipe) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  if (!(pipe->state->flags & PIPE_WRITER_BLOCKED))
    return;
  assert(pipe->state->stream_writer);
  if (pipe->state->flags & PIPE_WRITER_WOKEN)
    return;
  pipe->state->flags |= PIPE_WRITER_WOKEN;
  wasip3_waitable_status_t status =
      stream_read_unit(pipe->state->stream_reader, NULL, 1);
  (void)status;
  assert(WASIP3_WAITABLE_STATE(status) == WASIP3_WAITABLE_COMPLETED);
  assert(WASIP3_WAITABLE_COUNT(status) == 1);
}

static void pipe_unblock_reader(pipe_t *pipe) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  if (!(pipe->state->flags & PIPE_READER_BLOCKED))
    return;
  assert(pipe->state->stream_reader);
  if (pipe->state->flags & PIPE_READER_WOKEN)
    return;
  pipe->state->flags |= PIPE_READER_WOKEN;
  wasip3_waitable_status_t status =
      stream_write_unit(pipe->state->stream_writer, NULL, 1);
  (void)status;
  assert(WASIP3_WAITABLE_STATE(status) == WASIP3_WAITABLE_COMPLETED);
  assert(WASIP3_WAITABLE_COUNT(status) == 1);
}

static int pipe_block_enter(pipe_t *pipe, int blocked, int woken) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  (void)woken;

  // Concurrent operations are not supported at this time.
  if (pipe->state->flags & blocked) {
    errno = EOPNOTSUPP;
    return -1;
  }
  assert(!(pipe->state->flags & woken));
  pipe->state->flags |= blocked;
  return 0;
}

static bool pipe_block_exit(pipe_t *pipe, int blocked, int woken) {
  STRONG_ASSERT_HELD(pipe->state->lock);

  bool ret = (pipe->state->flags & woken) != 0;
  pipe->state->flags &= ~woken;
  pipe->state->flags &= ~blocked;
  return ret;
}

static int pipe_block_reader_enter(pipe_t *pipe) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  if (pipe_block_enter(pipe, PIPE_READER_BLOCKED, PIPE_READER_WOKEN) < 0)
    return -1;
  pipe_ensure_stream(pipe);
  wasip3_waitable_status_t status =
      stream_read_unit(pipe->state->stream_reader, NULL, 1);
  (void)status;
  assert(status == WASIP3_WAITABLE_STATUS_BLOCKED);
  return 0;
}

static bool pipe_block_reader_exit(pipe_t *pipe) {
  bool woken = pipe_block_exit(pipe, PIPE_READER_BLOCKED, PIPE_READER_WOKEN);
  if (!woken) {
    wasip3_waitable_status_t status =
        stream_cancel_read_unit(pipe->state->stream_reader);
    (void)status;
    assert(WASIP3_WAITABLE_STATE(status) == WASIP3_WAITABLE_CANCELLED);
    assert(WASIP3_WAITABLE_COUNT(status) == 0);
  }
  return woken;
}

static int pipe_block_writer_enter(pipe_t *pipe) {
  STRONG_ASSERT_HELD(pipe->state->lock);
  if (pipe_block_enter(pipe, PIPE_WRITER_BLOCKED, PIPE_WRITER_WOKEN) < 0)
    return -1;
  pipe_ensure_stream(pipe);
  wasip3_waitable_status_t status =
      stream_write_unit(pipe->state->stream_writer, NULL, 1);
  (void)status;
  assert(status == WASIP3_WAITABLE_STATUS_BLOCKED);
  return 0;
}

static bool pipe_block_writer_exit(pipe_t *pipe) {
  bool woken = pipe_block_exit(pipe, PIPE_WRITER_BLOCKED, PIPE_WRITER_WOKEN);
  if (!woken) {
    wasip3_waitable_status_t status =
        stream_cancel_write_unit(pipe->state->stream_writer);
    (void)status;
    assert(WASIP3_WAITABLE_STATE(status) == WASIP3_WAITABLE_CANCELLED);
    assert(WASIP3_WAITABLE_COUNT(status) == 0);
  }
  return woken;
}

static void pipe_block_thread(uint32_t stream) {
  bool closed;
  size_t amt = __wasilibc_stream_block_on(WASIP3_WAITABLE_STATUS_BLOCKED,
                                          stream, &closed);
  (void)amt;
  assert(amt == 1);
  assert(!closed);
}

static void pipe_free(void *data) {
  pipe_t *pipe = (pipe_t *)data;

  bool free_state = false;
  {
    STRONG_LOCK(pipe->state->lock);
    defer STRONG_UNLOCK(pipe->state->lock);

    if (pipe->reader) {
      pipe->state->flags &= ~PIPE_READER_ALIVE;
    } else {
      pipe->state->flags &= ~PIPE_WRITER_ALIVE;
    }

    if (!(pipe->state->flags & (PIPE_READER_ALIVE | PIPE_WRITER_ALIVE))) {
      free_state = true;
    } else if (pipe->reader) {
      pipe_unblock_writer(pipe);
    } else {
      pipe_unblock_reader(pipe);
    }
  }

  if (free_state)
    pipe_state_free(pipe->state);

  free(pipe);
}

static int pipe_fstat(void *data, struct stat *buf) {
  (void)data;
  memset(buf, 0, sizeof(*buf));
  // Mirror what Linux seems to do for pipes, but otherwise these are mostly
  // made-up values.
  buf->st_mode = S_IFIFO | 0600;
  buf->st_nlink = 1;
  return 0;
}

static int pipe_fcntl_getfl(void *data) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  int ret = pipe->reader ? O_RDONLY : O_WRONLY;
  if (!pipe->blocking)
    ret |= O_NONBLOCK;
  return ret;
}

static int pipe_fcntl_setfl(void *data, int flags) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  pipe->blocking = !(flags & O_NONBLOCK);
  return 0;
}

static int pipe_set_blocking(void *data, bool blocking) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  pipe->blocking = blocking;
  return 0;
}

static ssize_t pipe_recvfrom(void *data, void *buffer, size_t length, int flags,
                             struct sockaddr *addr, socklen_t *addrlen) {
  if (flags || addr || addrlen) {
    errno = EINVAL;
    return -1;
  }

  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  if (!pipe->reader) {
    errno = EBADF;
    return -1;
  }

  // Handle zero-length reads specially up-front.
  if (length == 0)
    return 0;

  while (1) {
    // If there's data in the pipe, then pull it out and wake up the writer, if
    // there, as it might have space to put things now.
    if (pipe->state->buffer_len) {
      size_t amt =
          length < pipe->state->buffer_len ? length : pipe->state->buffer_len;
      memcpy(buffer, pipe->state->buffer, amt);
      memmove(pipe->state->buffer, pipe->state->buffer + amt,
              pipe->state->buffer_len - amt);
      pipe->state->buffer_len -= amt;
      pipe_unblock_writer(pipe);
      return amt;
    }

    // If the writer isn't actually present, and there's no data, then we're at
    // EOF.
    if (!(pipe->state->flags & PIPE_WRITER_ALIVE))
      return 0;

    // No data and the writer's still out there. Time to block. Skip this if
    // it's a nonblocking pipe, however.
    if (!pipe->blocking) {
      errno = EWOULDBLOCK;
      return -1;
    }

    // Disallow concurrent blocking reads for now, and then actually block the
    // current thread. Note that the block is specifically done outside of this
    // pipe's lock to allow writes to progress independently.
    if (pipe_block_reader_enter(pipe) < 0)
      return -1;
    STRONG_UNLOCK(pipe->state->lock);
    pipe_block_thread(pipe->state->stream_reader);
    STRONG_LOCK(pipe->state->lock);
    pipe_block_reader_exit(pipe);
  }
}

static ssize_t pipe_sendto(void *data, const void *buffer, size_t length,
                           int flags, const struct sockaddr *addr,
                           socklen_t addrlen) {
  if (flags || addr || addrlen) {
    errno = EINVAL;
    return -1;
  }

  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  if (pipe->reader) {
    errno = EBADF;
    return -1;
  }

  // Handle zero-length writes as a special-case first.
  if (length == 0)
    return 0;

  // Lazily allocate the buffer. For now a fixed size, probably should make this
  // configurable somehow in the future.
  if (!pipe->state->buffer) {
    pipe->state->buffer_cap = PIPE_BUF;
    pipe->state->buffer = malloc(pipe->state->buffer_cap);
    if (!pipe->state->buffer) {
      errno = ENOMEM;
      return -1;
    }
  }

  while (1) {
    // If the reader's gone then this write's not going anywhere, return an
    // error.
    if (!(pipe->state->flags & PIPE_READER_ALIVE)) {
      errno = EPIPE;
      return -1;
    }

    // POSIX requires writes of up to PIPE_BUF bytes to be atomic: they either
    // transfer everything or block/fail, never a partial transfer. Larger
    // writes may transfer whatever fits.
    size_t remaining = pipe->state->buffer_cap - pipe->state->buffer_len;
    bool atomic = length <= PIPE_BUF;
    if (remaining > 0 && (!atomic || length <= remaining)) {
      size_t amt = length < remaining ? length : remaining;
      memcpy(pipe->state->buffer + pipe->state->buffer_len, buffer, amt);
      pipe->state->buffer_len += amt;
      pipe_unblock_reader(pipe);
      return amt;
    }

    // If there's no space in the buffer, or this atomic write doesn't fit,
    // then it's time to block. Similar to the read path above this only
    // supports one concurrent blocked writer at this time.
    if (!pipe->blocking) {
      errno = EWOULDBLOCK;
      return -1;
    }
    if (pipe_block_writer_enter(pipe) < 0)
      return -1;
    STRONG_UNLOCK(pipe->state->lock);
    pipe_block_thread(pipe->state->stream_writer);
    STRONG_LOCK(pipe->state->lock);
    pipe_block_writer_exit(pipe);
  }
}

static void pipe_read_ready(void *data, poll_state_t *state,
                            wasip3_event_t *event) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);
  bool woken = pipe_block_reader_exit(pipe);
  (void)woken;
  if (event) {
    assert(woken);
    assert(event->event == WASIP3_EVENT_STREAM_READ);
    assert(event->waitable == pipe->state->stream_reader);
    assert(WASIP3_WAITABLE_STATE(event->code) == WASIP3_WAITABLE_COMPLETED);
    assert(WASIP3_WAITABLE_COUNT(event->code) == 1);
    __wasilibc_poll_ready(state, POLLRDNORM);
  }
}

static void pipe_write_ready(void *data, poll_state_t *state,
                             wasip3_event_t *event) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);
  bool woken = pipe_block_writer_exit(pipe);
  (void)woken;
  if (event) {
    assert(woken);
    assert(event->event == WASIP3_EVENT_STREAM_WRITE);
    assert(event->waitable == pipe->state->stream_writer);
    assert(WASIP3_WAITABLE_STATE(event->code) == WASIP3_WAITABLE_COMPLETED);
    assert(WASIP3_WAITABLE_COUNT(event->code) == 1);
    __wasilibc_poll_ready(state, POLLWRNORM);
  }
}

static int pipe_poll_register(void *data, poll_state_t *state, short events) {
  pipe_t *pipe = (pipe_t *)data;
  STRONG_LOCK(pipe->state->lock);
  defer STRONG_UNLOCK(pipe->state->lock);

  if ((events & POLLRDNORM) != 0 && pipe->reader) {
    if (pipe->state->buffer_len || !(pipe->state->flags & PIPE_WRITER_ALIVE)) {
      __wasilibc_poll_ready(state, POLLRDNORM);
      return 0;
    }
    if (pipe_block_reader_enter(pipe) < 0)
      return -1;
    if (__wasilibc_poll_add(state, pipe->state->stream_reader, pipe_read_ready,
                            data) < 0) {
      pipe_block_reader_exit(pipe);
      return -1;
    }
  }
  if ((events & POLLWRNORM) != 0 && !pipe->reader) {
    if (pipe->state->buffer_cap == 0 ||
        pipe->state->buffer_len < pipe->state->buffer_cap ||
        !(pipe->state->flags & PIPE_READER_ALIVE)) {
      __wasilibc_poll_ready(state, POLLWRNORM);
      return 0;
    }
    if (pipe_block_writer_enter(pipe) < 0)
      return -1;
    if (__wasilibc_poll_add(state, pipe->state->stream_writer, pipe_write_ready,
                            data) < 0) {
      pipe_block_writer_exit(pipe);
      return -1;
    }
  }

  return 0;
}

static descriptor_vtable_t pipe_vtable = {
    .free = pipe_free,
    .fstat = pipe_fstat,
    .fcntl_getfl = pipe_fcntl_getfl,
    .fcntl_setfl = pipe_fcntl_setfl,
    .set_blocking = pipe_set_blocking,
    .sendto = pipe_sendto,
    .recvfrom = pipe_recvfrom,
    .poll_register = pipe_poll_register,
};

#endif // __wasip3__

int pipe(int fd[2]) { return pipe2(fd, 0); }

int pipe2(int fd[2], int flags) {
  if ((flags & ~(O_CLOEXEC | O_NONBLOCK)) != 0) {
    errno = EINVAL;
    return -1;
  }
#if defined(__wasip1__) || defined(__wasip2__) || defined(__webp2__)
  (void)fd;
  WASILIBC_LOG_UNSUPPORTED("pipe2");
  errno = ENOTSUP;
  return -1;
#else
  pipe_t *reader = calloc(1, sizeof(pipe_t));
  pipe_t *writer = calloc(1, sizeof(pipe_t));
  pipe_state_t *state = calloc(1, sizeof(pipe_state_t));

  if (!reader || !writer || !state) {
    free(reader);
    free(writer);
    free(state);
    errno = ENOMEM;
    return -1;
  }

  state->flags |= PIPE_READER_ALIVE;
  state->flags |= PIPE_WRITER_ALIVE;
  reader->state = state;
  reader->reader = true;
  reader->blocking = !(flags & O_NONBLOCK);
  writer->state = state;
  writer->reader = false;
  writer->blocking = !(flags & O_NONBLOCK);

  state = NULL;

  descriptor_table_entry_t reader_entry = {
      .data = &reader->cnt,
      .vtable = &pipe_vtable,
  };
  descriptor_table_entry_t writer_entry = {
      .data = &writer->cnt,
      .vtable = &pipe_vtable,
  };

  int rd = descriptor_table_insert(reader_entry);
  int wr = descriptor_table_insert(writer_entry);
  if (rd < 0 || wr < 0) {
    if (rd >= 0)
      descriptor_table_remove(rd);
    if (wr >= 0)
      descriptor_table_remove(wr);
    return -1;
  }

  fd[0] = rd;
  fd[1] = wr;
  return 0;
#endif
}
