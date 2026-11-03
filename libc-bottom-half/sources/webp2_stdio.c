#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddefer.h>
#include <stdio.h>
#include <unistd.h>
#include <wasi/descriptor_table.h>
#include <wasi/report-error.h>
#include <wasi/utf8.h>
#include <wasi/webp2.h>
#include <wasi/webp2_stdio.h>

#define LINE_BUF_SIZE 4096

typedef struct {
  // Must be the first field so that descriptor table `data` pointers can have
  // type `descriptor_refcnt_t *` but be cast to `stdio_t *`.
  descriptor_refcnt_t refcnt;
  bool warn;
  bool closed;
  wasilibc_utf8_writer_t utf8;
  uint8_t buf[LINE_BUF_SIZE];
} stdio_t;

static stdio_t __stdin;
static stdio_t __stdout;
static stdio_t __stderr;

static void stdio_console_out(stdio_t *stdio) {
  webp2_string_t out =
      (webp2_string_t){.ptr = stdio->utf8.buf, .len = stdio->utf8.valid_len};
  if (stdio->warn) {
    webp2_console_warn(&out);
  } else {
    webp2_console_log(&out);
  }
}

static void stdio_flush_to_console(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio->utf8.valid_len > 0) {
    stdio_console_out(stdio);
    __wasilibc_utf8_reset(&stdio->utf8);
  }
}

// Flushes all data in line buffer to the console, replacing any trailing
// multi-byte code points with the replacement character.
static void finish_and_flush(stdio_t *stdio) {
  if (stdio->closed) {
    return;
  }
  if (!__wasilibc_utf8_abort_current(&stdio->utf8)) {
    // Emit what is already valid in the buffer first, and carry over the
    // in-progress codepoint
    stdio_flush_to_console(stdio);
  }
  __wasilibc_utf8_abort_current(&stdio->utf8);
  stdio_flush_to_console(stdio);
}

static void stdio_free(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio == &__stdout || stdio == &__stderr) {
    // Don't flush musl's buffers, but do flush everything we have.
    finish_and_flush(stdio);
  }
  stdio->closed = true;
}

static int stdio_fstat(void *data, struct stat *buf) {
  (void)data;
  memset(buf, 0, sizeof(*buf));
  buf->st_mode = S_IFCHR;
  return 0;
}

static int stdio_fcntl_getfl(void *data) {
  stdio_t *stdio = (stdio_t *)data;
  if (stdio == &__stdin) {
    return O_RDONLY;
  } else {
    return O_WRONLY;
  }
}

static ssize_t stdio_read(void *data, void *buf, size_t nbyte) {
  (void)data;
  (void)buf;
  if (nbyte > SSIZE_MAX) {
    errno = EINVAL;
    return -1;
  }
  return 0; // immediate EOF
}

static int stdio_set_blocking(void *data, bool blocking) {
  // There's no difference between "blocking" and "non-blocking" IO for our
  // stdio implementation, so this does nothing.
  (void)data;
  (void)blocking;
  return 0;
}

static ssize_t stdio_write(void *data, const void *buf, size_t nbyte) {
  stdio_t *stdio = (stdio_t *)data;
  wasilibc_utf8_writer_t *utf8 = &stdio->utf8;
  for (size_t i = 0; i < nbyte; i++) {
    uint8_t b = ((uint8_t *)buf)[i];
    // First write to the buffer, flushing first if there was no room for the
    // results (which can occur either because the buffer was completely full
    // or because there was no room for a replacement char).
    bool already_logged = false;
    if (!__wasilibc_utf8_write(utf8, b)) {
      stdio_console_out(stdio);
      __wasilibc_utf8_reset(utf8);
      __wasilibc_utf8_write(utf8, b); // infallible
      already_logged = true;
    }
    // Then, if there was a newline, drop it from the buffer and flush.
    if (b == '\n') {
      assert(utf8->valid_len == utf8->raw_len);
      utf8->raw_len--;
      utf8->valid_len--;
      bool skip = utf8->valid_len == 0 && already_logged;
      if (!skip) {
        stdio_console_out(stdio);
      }
      __wasilibc_utf8_reset(utf8);
    }
  }
  return nbyte;
}

static descriptor_vtable_t stdin_vtable = {
    .read = stdio_read,
    .fstat = stdio_fstat,
    .fcntl_getfl = stdio_fcntl_getfl,
    .set_blocking = stdio_set_blocking,
};

static descriptor_vtable_t stdouterr_vtable = {
    .free = stdio_free,
    .write = stdio_write,
    .fflush = stdio_flush_to_console,
    .fstat = stdio_fstat,
    .fcntl_getfl = stdio_fcntl_getfl,
    .set_blocking = stdio_set_blocking,
};

static int stdio_add(stdio_t *stdio, descriptor_vtable_t *vtable) {
  descriptor_table_entry_t entry = (descriptor_table_entry_t){
      .data = (descriptor_refcnt_t *)stdio,
      .vtable = vtable,
  };
  return descriptor_table_insert(entry);
}

int __wasilibc_init_stdio() {
  if (stdio_add(&__stdin, &stdin_vtable) < 0) {
    return -1;
  }

  __stdout.utf8 = __wasilibc_make_utf8_writer(__stdout.buf, LINE_BUF_SIZE);
  if (stdio_add(&__stdout, &stdouterr_vtable) < 0) {
    return -1;
  }

  __stderr.utf8 = __wasilibc_make_utf8_writer(__stderr.buf, LINE_BUF_SIZE);
  __stderr.warn = true;
  if (stdio_add(&__stderr, &stdouterr_vtable) < 0) {
    return -1;
  }

  return 0;
}

void __wasilibc_webp2_fflush(int fd) {
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fd, &entry) < 0) {
    return;
  }
  defer descriptor_table_entry_dec(entry);
  if (entry.vtable->fflush) {
    entry.vtable->fflush(entry.data);
  }
}

void __wasilibc_webp2_flush_and_close_stdio(bool also_musl) {
  if (also_musl) {
    fflush(stdout);
  }
  finish_and_flush(&__stdout);
  __stdout.closed = true;

  if (also_musl) {
    fflush(stderr);
  }
  finish_and_flush(&__stderr);
  __stderr.closed = true;
}
