// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <_/cdefs.h>
#include <stdnoreturn.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>
#include <wasi/webp2_stdio.h>

noreturn void _Exit(int status) {
#if defined(__wasip1__)
  __wasi_proc_exit(status);
#elif defined(__wasip2__) || defined(__wasip3__)
  if (status >= 0 && status <= 255)
    exit_exit_with_code(status);
  exit_result_void_void_t exit_status = { .is_err = status != 0 };
  exit_exit(&exit_status);
#elif defined(__webp2__)
  // POSIX says not to flush buffers, so we don't flush musl's buffers, but
  // because our line buffering exists solely for the purpose of pretty
  // console.logs, we do flush those.
  __wasilibc_webp2_flush_and_close_stdio(/*also_musl=*/false);

  // We really don't have any other way to halt execution except to trap. Quite
  // unfortunate that this will manifest as a RuntimeError.
  if (status == 0) {
    webp2_string_t msg;
    webp2_string_set(&msg, "exit with status 0 (reported as trap)");
    webp2_console_log(&msg);
  } else {
    __wasilibc_report_error("exit with nonzero status");
  }
  __builtin_trap();
#else
# error "Unknown WASI version"
#endif
}

__strong_reference(_Exit, _exit);
