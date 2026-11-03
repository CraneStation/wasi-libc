#include <assert.h>
#include <common/errors.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stddefer.h>
#include <string.h>
#include <wasi/file_utils.h>
#include <wasi/utf8.h>
#include <wasi/wasip3_block.h>

#ifdef __wasip2__
void __wasilibc_set_stream_errno(streams_own_error_t error) {
  filesystem_error_code_t code;

  if (filesystem_filesystem_error_code(io_error_borrow_error(error), &code)) {
    translate_error(&code);
  } else {
    errno = EIO;
  }

  io_error_error_drop_own(error);
}
#endif

#if defined(__wasip2__) || defined(__wasip3__)
int wasi_string_from_c(const char *s, wasi_string_t *out) {
  int len = __wasilibc_validate_utf8(s);
  if (len < 0) {
    errno = EILSEQ;
    return -1;
  }
  out->ptr = (uint8_t *)s;
  out->len = len;
  return 0;
}
#endif

#ifdef __wasip2__
int __wasilibc_pollable_block_on(poll_borrow_pollable_t pollable,
                                 monotonic_clock_duration_t timeout) {
  // Without a timeout all that's needed is to block on the pollable itself.
  if (timeout == 0) {
    poll_method_pollable_block(pollable);
    return 0;
  }

  // Otherwise wait on both `pollable` and a timer, and then afterwards
  // determine which one of the two it was that became ready. Note that if both
  // are ready then `pollable` wins as there's no need to report a timeout when
  // the operation can now make progress.
  monotonic_clock_own_pollable_t timeout_pollable =
      monotonic_clock_subscribe_duration(timeout);
  poll_borrow_pollable_t pollables_ptr[2] = {
      pollable,
      poll_borrow_pollable(timeout_pollable),
  };
  poll_list_borrow_pollable_t pollables = {
      .ptr = pollables_ptr,
      .len = 2,
  };
  wasip2_list_u32_t ret;
  poll_poll(&pollables, &ret);
  poll_pollable_drop_own(timeout_pollable);

  bool ready = false;
  for (size_t i = 0; i < ret.len; i++) {
    if (ret.ptr[i] == 0)
      ready = true;
  }
  wasip2_list_u32_free(&ret);
  if (ready)
    return 0;
  errno = EWOULDBLOCK;
  return -1;
}
#endif // __wasip2__

#ifdef __wasip3__
/// Update `state` with the result of `code` that happened.
static size_t wasip3_io_update_code(wasip3_io_state_t *state,
                                    wasip3_waitable_status_t code) {
  STRONG_ASSERT_HELD(*state->lock);
  assert(code != WASIP3_WAITABLE_STATUS_BLOCKED);
  switch (WASIP3_WAITABLE_STATE(code)) {
  case WASIP3_WAITABLE_COMPLETED:
  case WASIP3_WAITABLE_CANCELLED:
    break;
  case WASIP3_WAITABLE_DROPPED:
    state->flags |= WASIP3_IO_DONE;
    break;
  default:
    assert(0 && "unexpected waitable state");
    abort();
  }

  assert(state->flags & WASIP3_IO_INPROGRESS);
  state->flags &= ~WASIP3_IO_INPROGRESS;

  if (state->flags & WASIP3_IO_ZERO_INPROGRESS) {
    state->flags &= ~WASIP3_IO_ZERO_INPROGRESS;

    assert(!(state->flags & WASIP3_IO_SHOULD_BE_READY));
    state->flags |= WASIP3_IO_SHOULD_BE_READY;
  }

  return WASIP3_WAITABLE_COUNT(code);
}

/// Update `state` with the result of `event` that happened.
static size_t wasip3_io_update_event(wasip3_io_state_t *state,
                                     wasip3_event_t *event) {
  assert(event->event == WASIP3_EVENT_STREAM_WRITE ||
         event->event == WASIP3_EVENT_STREAM_READ);
  assert(event->waitable == state->stream);
  return wasip3_io_update_code(state, event->code);
}

/// When `event` has happened due to a completion of a pending write, this
/// function will advance `state` forward.
///
/// This attempts to perform any follow-up writes as necessary if the pending
/// write ended up coming in short. Additionally this will clear out the
/// internal buffered data once it reaches
/// completion.
///
/// Returns `true` if this stream is ready for more writes, or `false` if
/// there's still a pending write in-flight.
static bool wasip3_advance_pending_write(wasip3_io_state_t *state,
                                         wasip3_event_t *event) {
  STRONG_ASSERT_HELD(*state->lock);
  // Update the I/O internal state given the result of the write.
  state->buf_start += wasip3_io_update_event(state, event);

  // While there's remaining writes to perform, kick those off here. Once a
  // write blocks we bail out of this loop as there's I/O in-progress.
  while (!(state->flags & WASIP3_IO_DONE) &&
         state->buf_start != state->buf_end) {
    wasip3_waitable_status_t status =
        filesystem_stream_u8_write(state->stream, state->buf + state->buf_start,
                                   state->buf_end - state->buf_start);
    state->flags |= WASIP3_IO_INPROGRESS;
    if (status == WASIP3_WAITABLE_STATUS_BLOCKED)
      return false;
    state->buf_start += wasip3_io_update_code(state, status);
  }

  // Everything should be done now at this point, meaning that the stream is
  // closed or we've written the entire buffer. Clean up internal state and
  // return to indicate there's no more pending I/O.
  assert((state->flags & WASIP3_IO_DONE) || state->buf_start == state->buf_end);
  free(state->buf);
  state->buf = NULL;
  state->buf_start = 0;
  state->buf_end = 0;
  return true;
}

/// Entrypoint for I/O on reads/writes.
///
/// This will synchronize with other concurrent operations and notably ensure
/// that this operation completes with an exclusive lock. If `blocking` is
/// `true` then this will block waiting for other operations to complete. If
/// it's set to `false` this may briefly block if a race meant that another
/// thread is temporarily unscheduled, but this will never block indefinitely.
static int wasip3_io_sync(wasip3_io_state_t *state, bool blocking) {
  STRONG_ASSERT_HELD(*state->lock);

  // If there's another thread blocked in an I/O operation at this time, then
  // this one can't make any progress as the stream is in-use and the component
  // model only allows at most one in-flight operation at this time. If this is
  // a nonblocking operation then this immediately exits. Otherwise if this is
  // a blocking operation it's our responsibility to wait here until the I/O
  // object is ready. To do that we drop the state's lock and then acquire the
  // `blocking_lock`. This lock is held for the duration of blocking I/O
  // operations which means we can't acquire it until it's finished. Once it's
  // acquired it's immediately dropped and then the whole situation is retried.
  //
  // Note that when `state->lock` is dropped we're sure to leave at least a
  // breadcrumb behind indicating that `state` is still being used. This is
  // intended to reject any calls to reset `state` such as a TCP `shutdown`
  // or a file `seek` which resets streams.
  while (state->flags & WASIP3_IO_BLOCKED) {
    if (!blocking) {
      errno = EOPNOTSUPP;
      return -1;
    }
#ifdef _REENTRANT
    state->blocking_waiters++;
    STRONG_UNLOCK(*state->lock);

    // just a bounce to wait for any pending blocking I/O to finish.
    STRONG_LOCK(state->blocking_lock);
    STRONG_UNLOCK(state->blocking_lock);

    STRONG_LOCK(*state->lock);
    state->blocking_waiters--;
#else
    // should not be possible without threads
    __builtin_trap();
#endif
  }

  return 0;
}

/// Indicator that a blocking I/O operation on `state` is about to be performed.
///
/// This manages the internals of `state` to ensure that it's flagged that a
/// thread is doing blocking I/O which will prevent other threads from doing
/// I/O. Internally this will acquire the state's `blocking_lock`, which should
/// not be held by any other thread, and will then return.
///
/// This must be paired with a call to `wasip3_exit_blocking_operation` when the
/// I/O is complete to unblock other operations.
///
/// After calling this function it's safe to drop `state`'s lock.
static void wasip3_enter_blocking_operation(wasip3_io_state_t *state) {
  STRONG_ASSERT_HELD(*state->lock);
  assert(!(state->flags & WASIP3_IO_BLOCKED));
  state->flags |= WASIP3_IO_BLOCKED;
  STRONG_LOCK(state->blocking_lock);
}

/// Exit routine for a blocking I/O operation.
///
/// Previously `wasip3_enter_blocking_operation` must have been called with
/// `state` and this undoes the work configured there. The `state`'s `lock` must
/// be acquired before entering this function.
static void wasip3_exit_blocking_operation(wasip3_io_state_t *state) {
  STRONG_ASSERT_HELD(state->blocking_lock);
  STRONG_ASSERT_HELD(*state->lock);
  STRONG_UNLOCK(state->blocking_lock);
  assert(state->flags & WASIP3_IO_BLOCKED);
  state->flags &= ~WASIP3_IO_BLOCKED;
}

/// Attempts to resolve any pending write that may be in-progress on `write`.
///
/// This may notably end up issuing more writes to finish a buffered write that
/// was previously flagged as completed.
static int wasip3_write_resolve_pending(wasi_write_t *write) {
  wasip3_event_t event;
  wasip3_io_state_t *state = write->state;
  STRONG_ASSERT_HELD(*state->lock);
  assert(!(state->flags & WASIP3_IO_BLOCKED));

  // If there's nothing in-progress, then there's nothing to do, so bail out.
  if (!(state->flags & WASIP3_IO_INPROGRESS))
    return 0;
  assert(state->buf || (state->flags & WASIP3_IO_ZERO_INPROGRESS));

  // This loop ensures that the entirety of `write->buf` is written out. Libc
  // already reported that the write succeeded so if a short write happens then
  // it's got to get started back up again.
  while (1) {
    // For blocking I/O this awaits the previous result. For non-blocking I/O a
    // `poll` operation is done.
    if (write->blocking) {
      wasip3_enter_blocking_operation(state);
      STRONG_UNLOCK(*state->lock);
      bool ok =
          __wasilibc_waitable_block_on(state->stream, &event, write->timeout);
      STRONG_LOCK(*state->lock);
      wasip3_exit_blocking_operation(state);
      if (!ok) {
        errno = ETIMEDOUT;
        return -1;
      }
    } else {
      __wasilibc_poll_waitable(state->stream, &event);
      if (event.event == WASIP3_EVENT_NONE) {
        errno = EWOULDBLOCK;
        return -1;
      }
    }

    // Update the internal status of this stream with the `event` we have now
    // learned. If the stream is complete at this point then go ahead and
    // return.
    if (wasip3_advance_pending_write(state, &event))
      return 0;

    // If the write isn't blocking then a pending I/O op is kicked off from
    // above and there's no point in turning the loop and re-polling. Bail out
    // here with EWOULDBLOCK.
    if (!write->blocking) {
      assert(state->flags & WASIP3_IO_INPROGRESS);
      errno = EWOULDBLOCK;
      return -1;
    }
  }

  abort();
}

/// Helper of `read`/`write` starting below.
static bool wasip3_start_zero_length(wasip3_io_state_t *state,
                                     wasip3_waitable_status_t status) {
  STRONG_ASSERT_HELD(*state->lock);
  assert(!(state->flags & WASIP3_IO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_ZERO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_SHOULD_BE_READY));
  assert(!(state->flags & WASIP3_IO_DONE));
  state->flags |= WASIP3_IO_INPROGRESS;
  state->flags |= WASIP3_IO_ZERO_INPROGRESS;
  if (status == WASIP3_WAITABLE_STATUS_BLOCKED)
    return true;
  wasip3_io_update_code(state, status);
  return false;
}

/// Starts a zero-length write on the stream pointed to by `state`.
///
/// This requires that there's no active I/O on the stream at this time and
/// that it's not a closed stream.
static bool wasip3_write_start_zero_length(wasip3_io_state_t *state) {
  return wasip3_start_zero_length(
      state, filesystem_stream_u8_write(state->stream, NULL, 0));
}

/// Starts a zero-length read on the stream pointed to by `state`.
///
/// This requires that there's no active I/O on the stream at this time and
/// that it's not a closed stream.
static bool wasip3_read_start_zero_length(wasip3_io_state_t *state) {
  return wasip3_start_zero_length(
      state, filesystem_stream_u8_read(state->stream, NULL, 0));
}

/// Starts a nonzero-length read on the stream pointed to by `state`.
///
/// This requires that there's no active I/O on the stream at this time and
/// that it's not a closed stream.
static int wasip3_read_start_nonzero(wasip3_io_state_t *state, size_t length) {
  STRONG_ASSERT_HELD(*state->lock);
  assert(state->flags & WASIP3_IO_MUST_BUFFER);
  assert(!(state->flags & WASIP3_IO_SHOULD_BE_READY));
  assert(!(state->flags & WASIP3_IO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_ZERO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_DONE));
  assert(state->buf == NULL);
  assert(length);
  state->buf = malloc(length);
  if (!state->buf) {
    errno = ENOMEM;
    return -1;
  }
  state->buf_start = 0;
  state->buf_end = 0;
  wasip3_waitable_status_t status =
      filesystem_stream_u8_read(state->stream, state->buf, length);
  state->flags |= WASIP3_IO_INPROGRESS;
  if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
    errno = EWOULDBLOCK;
    return -1;
  }
  state->buf_end = wasip3_io_update_code(state, status);
  return 0;
}

static size_t wasip3_read_complete_internally(wasip3_io_state_t *state,
                                              void *buffer, size_t length) {
  STRONG_ASSERT_HELD(*state->lock);
  size_t buf_len = state->buf_end - state->buf_start;
  size_t amount = buf_len < length ? buf_len : length;
  memcpy(buffer, state->buf + state->buf_start, amount);
  state->buf_start += amount;
  if (state->buf_start == state->buf_end) {
    free(state->buf);
    state->buf = NULL;
    state->buf_start = 0;
    state->buf_end = 0;
  }
  return amount;
}

void wasip3_write_state_close(wasip3_io_state_t *state) {
  // If this was never initialized then we don't have a lock to check, but if
  // it's even been initialized then the lock can be checked to be held.
#ifdef _REENTRANT
  if (state->lock) {
    STRONG_ASSERT_HELD(*state->lock);
  } else {
    assert(!(state->flags & WASIP3_IO_INPROGRESS));
  }
#endif

  // Component model streams can't be closed with active I/O operations. If
  // there's an operation in-flight then there are two options:
  //
  // 1. Cancel it. This would be a problem though because the data has already
  //    been acknowledged to be sent via POSIX-style APIs, meaning that it would
  //    effectively corrupt the stream being sent.
  // 2. Wait on it. This makes `close` a blocking operation if a previous
  //    nonblocking `write` was issued.
  //
  // Neither of these options are great and this is additionally the subject of
  // discussion at WebAssembly/component-model#617. For now the best that can be
  // done is blocking where a future `try-*` intrinsic will remove the need to
  // block here.
  if ((state->flags & WASIP3_IO_INPROGRESS) &&
      !(state->flags & WASIP3_IO_ZERO_INPROGRESS)) {
    wasi_write_t write = {0};
    write.state = state;
    write.blocking = true;
    int rc = wasip3_write_resolve_pending(&write);
    (void)rc;
    assert(rc == 0);
    assert(!(state->flags & WASIP3_IO_INPROGRESS));
  }

  STRONG_ASSERT_EMPTY(state->blocking_lock);
  if (state->flags & WASIP3_IO_INPROGRESS)
    filesystem_stream_u8_cancel_write(state->stream);
  if (state->buf)
    free(state->buf);
  if (state->stream != 0)
    filesystem_stream_u8_drop_writable(state->stream);
  memset(state, 0, sizeof(*state));
}
#endif // __wasip3__

#if defined(__wasip2__) || defined(__wasip3__)
/// Internal implementation of `__wasilibc_write` except that this doesn't
/// update `write->offset`. That's done in the wrapper around this.
static ssize_t __wasilibc_write_without_offset_update(wasi_write_t *write,
                                                      const void *buffer,
                                                      size_t length) {
#if defined(__wasip2__)
  assert(write->output.__handle != 0);
  while (true) {
    streams_stream_error_t error;
    uint64_t count;

    // See how many bytes can be written to this stream, if any.
    if (!streams_method_output_stream_check_write(write->output, &count,
                                                  &error))
      return wasip2_handle_write_error(error);

    if (count) {
      // If bytes can be written to the stream, then attempt the write.
      count = count < length ? count : length;
      wasip2_list_u8_t list = {.ptr = (uint8_t *)buffer, .len = count};
      if (!streams_method_output_stream_write(write->output, &list, &error))
        return wasip2_handle_write_error(error);

      // For blocking writes additionally perform a blocking flush to ensure
      // that the data makes its way to the destination.
      if (write->blocking) {
        bool ok =
            streams_method_output_stream_blocking_flush(write->output, &error);
        if (!ok)
          return wasip2_handle_write_error(error);
      }
      return count;
    }

    // This stream isn't currently writable, and this is a non-blocking
    // operation, so bail out.
    if (!write->blocking) {
      errno = EWOULDBLOCK;
      return -1;
    }

    // Lazily initialize the pollable if one hasn't already been created yet.
    if (write->pollable->__handle == 0)
      *write->pollable = streams_method_output_stream_subscribe(write->output);

    // Either wait for a timeout or indefinitely for this stream to become
    // writable. Once this is done loop around back to the beginning.
    if (__wasilibc_pollable_block_on(poll_borrow_pollable(*write->pollable),
                                     write->timeout) < 0)
      return -1;
  }
#elif defined(__wasip3__)
  wasip3_io_state_t *state = write->state;

  if (wasip3_io_sync(state, write->blocking) < 0)
    return -1;

  // If this stream is closed, for example with a TCP shutdown, then it's
  // closed and we're at EOF.
  if (state->stream == 0)
    return 0;

  // First resolve any pending I/O, should it exist.
  if (wasip3_write_resolve_pending(write) < 0)
    return -1;
  assert((state->flags & WASIP3_IO_INPROGRESS) == 0);
  assert((state->flags & WASIP3_IO_ZERO_INPROGRESS) == 0);

  // Follow posix semantics for zero-length writes which appear to return 0
  // without actually doing any I/O.
  if (length == 0)
    return 0;

  // If this stream is complete, then delegate to EOF.
  if (state->flags & WASIP3_IO_DONE)
    return 0;

  // For simplicity, handle blocking writes directly here. This involves
  // blocking with an optional timeout and handling the result.
  if (write->blocking) {
    state->flags &= ~WASIP3_IO_SHOULD_BE_READY;

    wasip3_enter_blocking_operation(state);
    STRONG_UNLOCK(*state->lock);
    bool done = false;
    ssize_t amount = __wasilibc_stream_block_on_timeout(
        filesystem_stream_u8_write(state->stream, buffer, length),
        state->stream, &done, write->timeout,
        filesystem_stream_u8_cancel_write);
    STRONG_LOCK(*state->lock);
    wasip3_exit_blocking_operation(state);

    if (done)
      state->flags |= WASIP3_IO_DONE;
    if (amount < 0)
      return -1;
    return amount;
  }

  // ... and everything below is about implementing nonblocking writes ...

  assert(!write->blocking);

  while (1) {
    bool should_be_ready = state->flags & WASIP3_IO_SHOULD_BE_READY;
    state->flags &= ~WASIP3_IO_SHOULD_BE_READY;

    // If this stream isn't forced to buffer then do an opportunistic write
    // with the user-provided buffer. If this doesn't work (it was blocked
    // instead of completing), but it should work (because a previous
    // zero-length operation succeeded which can sometimes mean it's ready),
    // then it means we must buffer from now on. No matter what though if the
    // write is in-progress then it's cancelled here as the user's input buffer
    // won't persist beyond this function call. Throughout writing/cancellation
    // if anything actually succeeded then that's immediately returned here.
    if (!(state->flags & WASIP3_IO_MUST_BUFFER)) {
      wasip3_waitable_status_t status =
          filesystem_stream_u8_write(state->stream, buffer, length);
      state->flags |= WASIP3_IO_INPROGRESS;
      if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
        if (should_be_ready)
          state->flags |= WASIP3_IO_MUST_BUFFER;
        status = filesystem_stream_u8_cancel_write(state->stream);
      }
      size_t amount = wasip3_io_update_code(state, status);
      if (amount > 0)
        return amount;
      if (state->flags & WASIP3_IO_DONE)
        return 0;
    }

    // If, at this point, we're still not forced to buffer then it means that a
    // write was attempted above, cancelled, and nothing was transferred. In
    // that case perform a zero-length write as a readiness test for this
    // stream.
    if (!(state->flags & WASIP3_IO_MUST_BUFFER)) {
      if (wasip3_write_start_zero_length(state)) {
        errno = EWOULDBLOCK;
        return -1;
      }
      if (state->flags & WASIP3_IO_DONE)
        return 0;

      // The zero-length write succeeded, so turn the loop again to retry the
      // write of the user-supplied buffer. Note that the I/O "should be ready"
      // flag will be set here which will poison the loop and avoid coming here
      // again if zero-length reads/writes don't actually work.
      assert(state->flags & WASIP3_IO_SHOULD_BE_READY);
      continue;
    }

    break;
  }

  // At this point we're in nonblocking mode, we're required to buffer data,
  // and most other flags should all be turned off. Here the input `buffer` is
  // copied into the internal `state` and the I/O operation is issued.
  assert(state->flags & WASIP3_IO_MUST_BUFFER);
  assert(!(state->flags & WASIP3_IO_SHOULD_BE_READY));
  assert(!(state->flags & WASIP3_IO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_ZERO_INPROGRESS));
  assert(!(state->flags & WASIP3_IO_DONE));
  assert(state->buf == NULL);
  state->buf = malloc(length);
  if (!state->buf) {
    errno = ENOMEM;
    return -1;
  }
  memcpy(state->buf, buffer, length);
  state->buf_start = 0;
  state->buf_end = length;
  wasip3_waitable_status_t status =
      filesystem_stream_u8_write(state->stream, state->buf, length);
  state->flags |= WASIP3_IO_INPROGRESS;

  // If the I/O is blocked, then that's ok. The data is all owned by `state`
  // meaning that we've effectively just faked a write of `length` bytes. Here
  // it's reported as having written everything.
  if (status == WASIP3_WAITABLE_STATUS_BLOCKED)
    return length;

  // If that write actually succeeded, however, then deal with it here. That
  // means we have to deallocate the internal buffer and then handle the result
  // of the write.
  free(state->buf);
  state->buf = NULL;
  state->buf_start = 0;
  state->buf_end = 0;
  return wasip3_io_update_code(state, status);
#else
#error "Unknown WASI version"
#endif
}

ssize_t __wasilibc_write(wasi_write_t *write, const void *buffer,
                         size_t length) {
  ssize_t result =
      __wasilibc_write_without_offset_update(write, buffer, length);
  if (result > 0 && write->offset)
    *write->offset += result;
#ifndef __wasip2__
  if (result == 0 &&
      ((write->state->flags & WASIP3_IO_DONE) || (write->state->stream == 0)))
    return write->eof(write->eof_data);
#endif
  return result;
}

/// Internal helper for `__wasilibc_read` that dosen't update `read->offset`,
/// like the write helper above.
static ssize_t __wasilibc_read_without_offset_update(wasi_read_t *read,
                                                     void *buffer,
                                                     size_t length) {
#if defined(__wasip2__)
  while (true) {
    wasip2_list_u8_t result;
    streams_stream_error_t error;

    // Attempt a read for the `length` we were passed in.
    if (!streams_method_input_stream_read(read->input, length, &result, &error))
      return wasip2_handle_read_error(error);

    // For empty reads after performing a successful 0-length read go ahead and
    // bail out.
    if (length == 0)
      return 0;

    // If bytes were read then copy those to the output `buffer`, deallocate
    // the list that the canonical ABI allocated, and return.
    if (result.len) {
      size_t len = result.len;
      memcpy(buffer, result.ptr, len);
      wasip2_list_u8_free(&result);
      return len;
    }

    // Nonblocking reads bail out here as all that's left to do is block.
    if (!read->blocking) {
      errno = EWOULDBLOCK;
      return -1;
    }

    // Lazily initialize the pollable for this input stream.
    if (read->pollable->__handle == 0)
      *read->pollable = streams_method_input_stream_subscribe(read->input);

    // Wait either with a timeout or indefinitely for this read to complete.
    if (__wasilibc_pollable_block_on(poll_borrow_pollable(*read->pollable),
                                     read->timeout) < 0)
      return -1;
  }
#elif defined(__wasip3__)
  wasip3_io_state_t *state = read->state;
  wasip3_event_t event;

  if (wasip3_io_sync(state, read->blocking) < 0)
    return -1;

  // If this stream is closed, for example with a TCP shutdown, then it's
  // closed and we're at EOF.
  if (state->stream == 0)
    return 0;

  // If there's active I/O in progress for this stream then this must wait for
  // it to complete.
  if (state->flags & WASIP3_IO_INPROGRESS) {
    if (read->blocking) {
      wasip3_enter_blocking_operation(state);
      STRONG_UNLOCK(*state->lock);
      bool ok =
          __wasilibc_waitable_block_on(state->stream, &event, read->timeout);
      STRONG_LOCK(*state->lock);
      wasip3_exit_blocking_operation(state);

      if (!ok) {
        errno = ETIMEDOUT;
        return -1;
      }
    } else {
      __wasilibc_poll_waitable(state->stream, &event);
      if (event.event == WASIP3_EVENT_NONE) {
        errno = EWOULDBLOCK;
        return -1;
      }
    }
    // Upon completion that's the size of the read buffer which we're now
    // reading into.
    state->buf_end = wasip3_io_update_event(state, &event);
  }

  assert(!(state->flags & WASIP3_IO_INPROGRESS));

  // If this stream has internally buffered data that's ready to read then copy
  // that out here.
  if (state->buf)
    return wasip3_read_complete_internally(state, buffer, length);

  // Implement posix-like semantics where a `read` of 0 bytes doesn't do
  // anything and just returns 0.
  if (length == 0)
    return 0;

  // If this stream has finished, then delegate to EOF.
  if (state->flags & WASIP3_IO_DONE)
    return 0;

  // For simplicity handle the blocking read case here.
  if (read->blocking) {
    state->flags &= ~WASIP3_IO_SHOULD_BE_READY;
    bool done = false;
    wasip3_enter_blocking_operation(state);
    STRONG_UNLOCK(*state->lock);
    size_t amount = __wasilibc_stream_block_on_timeout(
        filesystem_stream_u8_read(read->state->stream, buffer, length),
        read->state->stream, &done, read->timeout,
        filesystem_stream_u8_cancel_read);
    STRONG_LOCK(*state->lock);
    wasip3_exit_blocking_operation(state);
    if (done)
      state->flags |= WASIP3_IO_DONE;
    if (amount < 0)
      return -1;
    return amount;
  }

  // ... and everything below is about implementing nonblocking reads ...

  // NB: this loop structure is intentionally quite similar to `write`'s
  // structure above. See there for more comments.
  while (1) {
    bool should_be_ready = state->flags & WASIP3_IO_SHOULD_BE_READY;
    state->flags &= ~WASIP3_IO_SHOULD_BE_READY;

    if (!(state->flags & WASIP3_IO_MUST_BUFFER)) {
      wasip3_waitable_status_t status =
          filesystem_stream_u8_read(state->stream, buffer, length);
      state->flags |= WASIP3_IO_INPROGRESS;
      if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
        if (should_be_ready)
          state->flags |= WASIP3_IO_MUST_BUFFER;
        status = filesystem_stream_u8_cancel_read(state->stream);
      }
      size_t amount = wasip3_io_update_code(state, status);
      if (amount > 0)
        return amount;
      if (state->flags & WASIP3_IO_DONE)
        return 0;
    }

    if (!(state->flags & WASIP3_IO_MUST_BUFFER)) {
      if (wasip3_read_start_zero_length(state)) {
        errno = EWOULDBLOCK;
        return -1;
      }
      if (state->flags & WASIP3_IO_DONE)
        return 0;

      assert(state->flags & WASIP3_IO_SHOULD_BE_READY);
      continue;
    }

    break;
  }

  // Like with writes above this is the point where we're forced to do internal
  // buffering of reads. Issue the read here referencing internal data and
  // return that we're blocked.
  if (wasip3_read_start_nonzero(state, length) < 0)
    return -1;
  return wasip3_read_complete_internally(state, buffer, length);
#else
#error "Unknown WASI version"
#endif
}

ssize_t __wasilibc_read(wasi_read_t *read, void *buffer, size_t length) {
  ssize_t result = __wasilibc_read_without_offset_update(read, buffer, length);
  if (result > 0 && read->offset)
    *read->offset += result;
#ifndef __wasip2__
  if (result == 0 &&
      ((read->state->flags & WASIP3_IO_DONE) || (read->state->stream == 0)))
    return read->eof(read->eof_data);
#endif
  return result;
}
#endif // __wasip2__ || __wasip3__

#ifdef __wasip3__
static void wasip3_poll_read_ready(void *data, poll_state_t *state,
                                   wasip3_event_t *event) {
  wasip3_io_state_t *iostate = (wasip3_io_state_t *)data;
  STRONG_LOCK(*iostate->lock);
  defer STRONG_UNLOCK(*iostate->lock);
  wasip3_exit_blocking_operation(iostate);

  if (event) {
    iostate->buf_end = wasip3_io_update_event(iostate, event);
    __wasilibc_poll_ready(state, POLLRDNORM);
  }
}

static void wasip3_poll_write_ready(void *data, poll_state_t *state,
                                    wasip3_event_t *event) {
  wasip3_io_state_t *iostate = (wasip3_io_state_t *)data;
  STRONG_LOCK(*iostate->lock);
  defer STRONG_UNLOCK(*iostate->lock);
  wasip3_exit_blocking_operation(iostate);

  if (event) {
    // Update our state with this event, and if the pending write is fully
    // complete then this is now ready for writing again. Otherwise there's
    // still a pending write so our job is complete trying to advance things a
    // bit.
    if (wasip3_advance_pending_write(iostate, event)) {
      __wasilibc_poll_ready(state, POLLWRNORM);
    } else {
      assert(iostate->flags & WASIP3_IO_INPROGRESS);
    }
  }
}

enum wasip3_poll_status {
  POLL_READY = 0,
  POLL_INPROGRESS = 1,
  POLL_ERROR = -1,
};

static enum wasip3_poll_status
wasip3_stream_poll_init(wasip3_io_state_t *iostate, short events) {
  // If the stream is closed then it's immediately ready for reading/writing as
  // that'll resolve with an error/0/etc.
  if (iostate->stream == 0)
    return POLL_READY;

  // If the I/O stream is finished it'll never block so it's always ready.
  if (iostate->flags & WASIP3_IO_DONE)
    return POLL_READY;

  // If there's I/O in progress, then that's what we're interested in so add it
  // to the set.
  if (iostate->flags & WASIP3_IO_INPROGRESS)
    return POLL_INPROGRESS;

  // For readable streams if there's buffered data then this is immediately
  // ready since that data can be read without blocking.
  if (events == POLLRDNORM && iostate->buf)
    return POLL_READY;

  if (iostate->flags & WASIP3_IO_MUST_BUFFER) {
    // For writable streams, if buffering is required and zero-length writes
    // don't work then this is immediately ready since those writes never block
    // (they're buffered internally).
    if (events == POLLWRNORM)
      return POLL_READY;

    // For readable streams if buffering is required then a read is kicked off
    // here. An arbitrary read size is chosen for now. If that read isn't ready
    // then the waitable is added, and otherwise it's flagged as immediately
    // ready.
    if (events == POLLRDNORM) {
      if (wasip3_read_start_nonzero(iostate, 16 * 1024) < 0) {
        if (errno == EWOULDBLOCK)
          return POLL_INPROGRESS;
        return POLL_ERROR;
      }
      return POLL_READY;
    }
  }

  // If I/O should be ready then that means a zero-length op has already
  // completed, so this should be good to go.
  if (iostate->flags & WASIP3_IO_SHOULD_BE_READY)
    return POLL_READY;

  // Kick off a zero-length write here. If that actually gets kicked off then
  // we're waiting on it so add it to the set. Otherwise it completed so we're
  // immediately ready.
  bool started_work = events == POLLWRNORM
                          ? wasip3_write_start_zero_length(iostate)
                          : wasip3_read_start_zero_length(iostate);
  if (started_work)
    return POLL_INPROGRESS;
  return POLL_READY;
}

static int wasip3_stream_poll(wasip3_io_state_t *iostate, poll_state_t *state,
                              short events, poll_ready_t ready) {
  // First ensure that `iostate` can enter a blocking operation I/O operation
  // `poll`. This ensures that the stream isn't currently in use by some other
  // thread's blocking I/O as moving things around in waitable sets wouldn't
  // work then. Note that if another thread is blocked at this time then this
  // will return a "not supported" error because that concurrent behavior isn't
  // supported by wasi-libc.
  if (wasip3_io_sync(iostate, false) < 0)
    return -1;

  switch (wasip3_stream_poll_init(iostate, events)) {
  // If this stream is immediately ready, then flag it as such within
  // `state.`
  case POLL_READY:
    __wasilibc_poll_ready(state, events);
    return 0;

  // If this stream has an in-progress I/O operation then it's gonna get added
  // to the waitable-set in `poll`. For that we flag this as entering a
  // blocking operation. Note that the `wasip3_exit_*` calls, both for this
  // blocking operation and the above "enter io", are done in the ready
  // callback.
  case POLL_INPROGRESS: {
    int rc = __wasilibc_poll_add(state, iostate->stream, ready, iostate);
    if (rc == 0)
      wasip3_enter_blocking_operation(iostate);
    return rc;
  }

  // On error we're just propagating the error.
  case POLL_ERROR:
    return -1;

  default:
    abort();
  }
}

int __wasilibc_read_poll(wasip3_io_state_t *iostate, poll_state_t *state) {
  return wasip3_stream_poll(iostate, state, POLLRDNORM, wasip3_poll_read_ready);
}

int __wasilibc_write_poll(wasip3_io_state_t *iostate, poll_state_t *state) {
  return wasip3_stream_poll(iostate, state, POLLWRNORM,
                            wasip3_poll_write_ready);
}
#endif // __wasip3__
