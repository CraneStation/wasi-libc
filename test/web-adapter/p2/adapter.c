// Test-only implementation of the `wasm:js` interfaces imported by the webp2
// target, written in terms of the libc of a stock `wasm32-wasip2` sysroot. See
// `adapter.wit` for more information.

#include "adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static void *xmalloc(size_t size) {
  void *ret = malloc(size);
  if (!ret)
    abort();
  return ret;
}

// ============================================================================
// `wasm:js/console`

// `console.log` always terminates its output with a newline. Note that this
// component is a reactor and never exits, so flush eagerly.
void exports_console_log(adapter_string_t *msg) {
  fwrite(msg->ptr, 1, msg->len, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  adapter_string_free(msg);
}

// Browsers show `console.warn` alongside `console.log`, but here it's mirrored
// on stderr so tests can distinguish the two.
void exports_console_warn(adapter_string_t *msg) {
  fwrite(msg->ptr, 1, msg->len, stderr);
  fputc('\n', stderr);
  fflush(stderr);
  adapter_string_free(msg);
}

// ============================================================================
// `Performance`
//
// `performance.timeOrigin` is the wall-clock time, in milliseconds, at which
// the environment started and `performance.now()` is the monotonic time in
// milliseconds elapsed since then. Here the "start" is the first time that
// either is needed.

struct exports_global_performance_t {
  double start;
  double time_origin;
};

static double clock_ms(clockid_t clock) {
  struct timespec ts;
  if (clock_gettime(clock, &ts) != 0)
    abort();
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static exports_global_performance_t *performance(void) {
  static bool init = false;
  static exports_global_performance_t perf;
  if (!init) {
    perf.start = clock_ms(CLOCK_MONOTONIC);
    perf.time_origin = clock_ms(CLOCK_REALTIME);
    init = true;
  }
  return &perf;
}

exports_global_own_performance_t exports_global_get_global_performance(void) {
  return exports_global_performance_new(performance());
}

double exports_global_method_performance_now(
    exports_global_borrow_performance_t self) {
  return clock_ms(CLOCK_MONOTONIC) - self->start;
}

double exports_global_method_get_performance_time_origin(
    exports_global_borrow_performance_t self) {
  return self->time_origin;
}

// All handles share the same static representation so there's nothing to do.
void exports_global_performance_destructor(exports_global_performance_t *rep) {
  (void)rep;
}

// ============================================================================
// `Date`

// `Date.now()` is an integral number of milliseconds since the Unix epoch.
double exports_global_static_date_now(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    abort();
  return (double)((long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// `date` has no constructor so this is never called.
void exports_global_date_destructor(exports_global_date_t *rep) {
  (void)rep;
  abort();
}

// ============================================================================
// `Error` and `reportError`

struct exports_global_error_t {
  adapter_option_string_t message;
};

exports_global_own_error_t exports_global_constructor_error(
    adapter_string_t *maybe_message,
    exports_global_new_error_options_t *maybe_options) {
  exports_global_error_t *error = xmalloc(sizeof(*error));
  error->message.is_some = maybe_message != NULL;
  if (maybe_message)
    error->message.val = *maybe_message;
  // `cause` isn't currently rendered anywhere, so discard it.
  if (maybe_options)
    exports_global_new_error_options_free(maybe_options);
  return exports_global_error_new(error);
}

void exports_global_error_destructor(exports_global_error_t *rep) {
  adapter_option_string_free(&rep->message);
  free(rep);
}

// Browsers log uncaught errors to the console, so mirror that on stderr. Note
// that `reportError` does not throw, so this returns normally.
void exports_global_report_error(exports_global_borrow_error_t throwable) {
  fputs("Uncaught Error", stderr);
  if (throwable->message.is_some)
    fprintf(stderr, ": %.*s", (int)throwable->message.val.len,
            (const char *)throwable->message.val.ptr);
  fputc('\n', stderr);
  fflush(stderr);
}

// ============================================================================
// `Uint8Array`
//
// Only the length of the array is tracked since contents never flow from the
// guest to the host through this type.

struct exports_global_uint_8_array_t {
  uint32_t length;
};

static exports_global_own_uint_8_array_t new_uint_8_array(uint32_t length) {
  exports_global_uint_8_array_t *array = xmalloc(sizeof(*array));
  array->length = length;
  return exports_global_uint_8_array_new(array);
}

exports_global_own_uint_8_array_t
exports_global_constructor_uint_8_array(uint32_t length) {
  return new_uint_8_array(length);
}

// Mirrors `TypedArray.prototype.subarray` for non-negative indices: both ends
// are clamped to the length of the array and `end` defaults to the length.
exports_global_own_uint_8_array_t exports_global_method_uint_8_array_subarray(
    exports_global_borrow_uint_8_array_t self, uint32_t begin,
    uint32_t *maybe_end) {
  uint32_t end = maybe_end ? *maybe_end : self->length;
  if (end > self->length)
    end = self->length;
  if (begin > end)
    begin = end;
  return new_uint_8_array(end - begin);
}

void exports_global_uint_8_array_destructor(
    exports_global_uint_8_array_t *rep) {
  free(rep);
}

// ============================================================================
// `Crypto`

struct exports_global_crypto_t {
  char unused;
};

static exports_global_crypto_t crypto;

exports_global_own_crypto_t exports_global_get_global_crypto(void) {
  return exports_global_crypto_new(&crypto);
}

void exports_global_method_crypto_get_random_values(
    exports_global_borrow_crypto_t self,
    exports_global_borrow_uint_8_array_t typed_array, adapter_list_u8_t *ret) {
  (void)self;
  // Browsers throw a `QuotaExceededError` for requests larger than this, so
  // mirror that limit to catch callers which don't respect it.
  uint32_t len = typed_array->length;
  if (len > 65536)
    abort();
  ret->len = len;
  ret->ptr = NULL;
  if (len == 0)
    return;
  // This is freed by the generated post-return function.
  ret->ptr = xmalloc(len);
  // `getentropy` is limited to 256 bytes per call.
  for (uint32_t i = 0; i < len; i += 256) {
    size_t chunk = len - i < 256 ? len - i : 256;
    if (getentropy(ret->ptr + i, chunk) != 0)
      abort();
  }
}

// All handles share the same static representation so there's nothing to do.
void exports_global_crypto_destructor(exports_global_crypto_t *rep) {
  (void)rep;
}
