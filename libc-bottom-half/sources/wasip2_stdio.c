#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/descriptor_table.h>
#include <wasi/stdio.h>
#include <wasi/wasip2.h>

#ifdef __wasip2__

typedef struct {
  descriptor_refcnt_t refcnt;
  int fd;
  // Lazily initialized streams. Depending on `fd` these may never be
  // initialized, for example stdin can never have an output stream.
  streams_own_input_stream_t input;
  streams_own_output_stream_t output;
  // Lazily initialized pollables derived from input/output.
  poll_own_pollable_t input_pollable;
  poll_own_pollable_t output_pollable;
  bool nonblocking;
} stdio_t;

static void stdio_free(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio->input_pollable.__handle != 0) {
    poll_pollable_drop_own(stdio->input_pollable);
  }
  if (stdio->output_pollable.__handle != 0) {
    poll_pollable_drop_own(stdio->output_pollable);
  }
  if (stdio->input.__handle != 0) {
    streams_input_stream_drop_own(stdio->input);
  }
  if (stdio->output.__handle != 0) {
    streams_output_stream_drop_own(stdio->output);
  }
  free(stdio);
}

static int stdio_get_read_stream(void *data, wasi_read_t *read) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio->fd != 0) {
    errno = EOPNOTSUPP;
    return -1;
  }
  if (stdio->input.__handle == 0)
    stdio->input = stdin_get_stdin();
  read->input = streams_borrow_input_stream(stdio->input);
  read->offset = NULL;
  read->pollable = &stdio->input_pollable;
  read->eof = NULL;
  read->timeout = 0;
  read->blocking = !stdio->nonblocking;
  return 0;
}

static int stdio_get_write_stream(void *data, wasi_write_t *write) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio->output.__handle == 0) {
    if (stdio->fd == 1) {
      stdio->output = stdout_get_stdout();
    } else if (stdio->fd == 2) {
      stdio->output = stderr_get_stderr();
    } else {
      errno = EOPNOTSUPP;
      return -1;
    }
  }
  write->output = streams_borrow_output_stream(stdio->output);
  write->offset = NULL;
  write->pollable = &stdio->output_pollable;
  write->blocking = !stdio->nonblocking;
  write->timeout = 0;
  return 0;
}

static int stdio_fstat(void *data, struct stat *buf) {
  (void)data;
  memset(buf, 0, sizeof(*buf));
  return 0;
}

static int stdio_fcntl_getfl(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  int flags = stdio->nonblocking ? O_NONBLOCK : 0;
  if (stdio->fd == 0) {
    return O_RDONLY | flags;
  } else {
    return O_WRONLY | flags;
  }
}

// Only O_NONBLOCK can change, as for a file; the other status flags
// (O_APPEND) mean nothing for a stream and are ignored.
static int stdio_fcntl_setfl(void *data, int flags) {
  stdio_t *stdio = (stdio_t *)data;
  stdio->nonblocking = (flags & O_NONBLOCK) != 0;
  return 0;
}

static int stdio_set_blocking(void *data, bool blocking) {
  stdio_t *stdio = (stdio_t *)data;
  stdio->nonblocking = !blocking;
  return 0;
}

static int stdio_isatty(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  switch (stdio->fd) {
  case 0: {
    terminal_stdin_own_terminal_input_t term_input;
    if (!terminal_stdin_get_terminal_stdin(&term_input))
      break;
    terminal_input_terminal_input_drop_own(term_input);
    return 1;
  }
  case 1: {
    terminal_stdout_own_terminal_output_t term_output;
    if (!terminal_stdout_get_terminal_stdout(&term_output))
      break;
    terminal_output_terminal_output_drop_own(term_output);
    return 1;
  }
  case 2: {
    terminal_stderr_own_terminal_output_t term_output;
    if (!terminal_stderr_get_terminal_stderr(&term_output))
      break;
    terminal_output_terminal_output_drop_own(term_output);
    return 1;
  }
  default:
    break;
  }

  errno = ENOTTY;
  return 0;
}

static descriptor_vtable_t stdio_vtable = {
    .free = stdio_free,
    .get_read_stream = stdio_get_read_stream,
    .get_write_stream = stdio_get_write_stream,
    .set_blocking = stdio_set_blocking,
    .fstat = stdio_fstat,
    .fcntl_getfl = stdio_fcntl_getfl,
    .fcntl_setfl = stdio_fcntl_setfl,
    .isatty = stdio_isatty,
};

static int stdio_add(int fd) {
  stdio_t *stdio = calloc(1, sizeof(stdio_t));
  if (!stdio) {
    errno = ENOMEM;
    return -1;
  }
  stdio->fd = fd;

  descriptor_table_entry_t entry;
  entry.vtable = &stdio_vtable;
  entry.data = &stdio->refcnt;
  return descriptor_table_insert(entry);
}

int __wasilibc_init_stdio() {
  if (stdio_add(0) < 0)
    return -1;
  if (stdio_add(1) < 0)
    return -1;
  if (stdio_add(2) < 0)
    return -1;
  return 0;
}
#endif // __wasip2__
