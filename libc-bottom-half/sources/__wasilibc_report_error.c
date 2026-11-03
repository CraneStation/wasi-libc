#include <stdarg.h>
#include <stdio.h>
#include <wasi/api.h>
#include <wasi/report-error.h>
#include <wasi/utf8.h>
#include <wasi/webp2_stdio.h>

void __wasilibc_report_error(const char *message) {
#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__)
  fprintf(stderr, "%s\n", message);
#elif defined(__webp2__)
  __wasilibc_webp2_report_error(message);
#endif
}

void __wasilibc_report_errorf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);

#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__)
  vfprintf(stderr, format, ap);
  fputc('\n', stderr);
#elif defined(__webp2__)
#define ERROR_MAX_LEN 512
  char format_buf[ERROR_MAX_LEN];
  uint8_t utf8_buf[ERROR_MAX_LEN + 1]; // leave room for nul

  int n = vsnprintf(format_buf, sizeof(format_buf), format, ap);
  wasilibc_utf8_writer_t utf8 =
      __wasilibc_make_utf8_writer(utf8_buf, ERROR_MAX_LEN);

  // Re-encode as UTF-8 (excluding vnsprintf's nul). Non-UTF-8 data can slip in
  // via __FILE__ or function names at a minimum.
  for (int i = 0; i < n && i < (int)sizeof(format_buf) - 1; i++) {
    if (!__wasilibc_utf8_write(&utf8, format_buf[i])) {
      break;
    }
  }
  __wasilibc_utf8_abort_current(&utf8);

  // Nul-terminate message and pass along
  utf8_buf[utf8.valid_len] = 0;
  __wasilibc_report_error((char *)utf8_buf);
#else
#error "Unknown WASI version"
#endif

  va_end(ap);
}

#ifdef __webp2__
void __wasilibc_webp2_report_error(const char *message) {
  __wasilibc_webp2_flush_and_close_stdio(/*also_musl=*/true);
  webp2_string_t msg;
  webp2_string_set(&msg, message);
  webp2_own_error_t err = webp2_constructor_error(&msg, NULL);
  webp2_report_error(webp2_borrow_error(err));
  webp2_error_drop_own(err);
}
#endif
