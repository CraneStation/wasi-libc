#ifdef _REENTRANT
#include <stdatomic.h>
#endif

#include <wasi/api.h>
#include <wasi/descriptor_table.h>
#include <wasi/report-error.h>

extern void __wasm_call_ctors(void);
extern int __main_void(void);
extern void __wasm_call_dtors(void);

#include "wasip3_symbol_references.h"

#if defined(__wasip1__)
__attribute__((export_name("_start"))) void _start(void)
#elif defined(__wasip2__)
// Note that this is manually doing what `wit-bindgen` might otherwise be
// doing. Given the special nature of this symbol this skip the typical
// `wit-bindgen` rigamarole and the signature of this function is simple enough
// that this shouldn't be too problematic (in theory).
__attribute__((export_name("wasi:cli/run@0.2.12#run"))) int _start(void)
#elif defined(__wasip3__)
__attribute__((export_name("wasi:cli/run@0.3.2#run"))) int _start(void)
#elif defined(__webp2__)
// On webp2 we export as _initialize so that the linker will call this from a
// start function instead of being called by the host.
__attribute__((export_name("_initialize"))) void _start(void)
#else
#error "Unsupported WASI version"
#endif
{
  // Commands should only be called once per instance. This simple check
  // ensures that the `_start` function isn't started more than once.
  //
  // We use `volatile` here to prevent the store to `started` from being
  // sunk past any subsequent code, and to prevent any compiler from
  // optimizing based on the knowledge that `_start` is the program
  // entrypoint.
#ifdef _REENTRANT
  static volatile _Atomic int started = 0;
  int expected = 0;
  if (!atomic_compare_exchange_strong(&started, &expected, 1)) {
    __builtin_trap();
  }
#else
  static volatile int started = 0;
  if (started != 0) {
    __builtin_trap();
  }
  started = 1;
#endif

  // The linker synthesizes this to call constructors.
  __wasm_call_ctors();

  // Call `__main_void` which will either be the application's zero-argument
  // `__main_void` function or a libc routine which obtains the command-line
  // arguments and calls `__main_argv_argc`.
  int r = __main_void();

  // On webp2 we don't run destructors because main is used as a start function
  // rather than the entire program. Everything about the application state
  // must therefore be ok after main exits.
#ifndef __webp2__
  // Call atexit functions, destructors, stdio cleanup, etc.
  __wasm_call_dtors();
#endif

#if defined(__wasip1__)
  // If main exited successfully, just return, otherwise call
  // `__wasi_proc_exit`.
  if (r != 0) {
    __wasi_proc_exit(r);
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  __wasilibc_assert_no_descriptor_leaks();
  return r != 0;
#elif defined(__webp2__)
  if (r != 0) {
    __wasilibc_webp2_report_error("main exited with nonzero status");
    __builtin_trap();
  }
#else
#error "Unsupported WASI version"
#endif
}
