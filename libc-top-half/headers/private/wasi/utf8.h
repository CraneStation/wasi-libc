#ifndef __WASI_UTF8_H
#define __WASI_UTF8_H

#include <features.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Validation

/**
 * Validates that `ptr_signed` is a valid utf-8 string.
 *
 * Returns the byte-length of the string on success, or -1 if it's invalid
 * utf-8.
 */
hidden int __wasilibc_validate_utf8(const char *ptr_signed);

// ============================================================================
// Writing

// Writes valid UTF-8 to a buffer. The range [0, valid_len) is always valid
// even when multi-byte code points are being written.
//
// `cap` must be at least 8.
typedef struct wasilibc_utf8_writer_t {
  uint8_t *buf;
  size_t cap;
  size_t raw_len, valid_len;
  size_t codepoint_remaining;
} wasilibc_utf8_writer_t;

#define __wasilibc_make_utf8_writer(_buf, _cap) \
  ((wasilibc_utf8_writer_t){ \
    .buf = (_buf), \
    .cap = (_cap), \
  })

// Emits a single byte to the writer's buffer. If the byte causes invalid
// UTF-8, a replacement character will be written instead. Returns false if
// it runs out of room in the buffer; in this case, you can either stop or call
// `wasilibc_utf8_reset` and then try again, which will always succeed.
hidden bool __wasilibc_utf8_write(wasilibc_utf8_writer_t *w, uint8_t b);

// Aborts the current multi-byte code point, if any, emitting a replacement
// character instead. Returns false if there is no room in the buffer; as with
// write, you can call `wasilibc_utf8_reset` and try again to guarantee
// success.
hidden bool __wasilibc_utf8_abort_current(wasilibc_utf8_writer_t *w);

// Resets the writer back to the start of its buffer. If there is a multi-byte
// code point in progress, it will be copied to the start of the buffer.
hidden void __wasilibc_utf8_reset(wasilibc_utf8_writer_t *w);

#endif // __WASI_UTF8_H
