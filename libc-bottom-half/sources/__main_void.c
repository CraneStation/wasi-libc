#include <stdlib.h>
#include <sysexits.h>
#include <wasi/api.h>

#ifndef __wasip1__
#include <string.h>
#include <wasi/libc-environ.h>
#endif

// The user's `main` function, expecting arguments.
//
// Note that we make this a weak symbol so that it will have a
// `WASM_SYM_BINDING_WEAK` flag in libc.so, which tells the dynamic linker that
// it need not be defined (e.g. in reactor-style apps with no main function).
// See also the TODO comment on `__main_void` below.
__attribute__((__weak__)) int __main_argc_argv(int argc, char *argv[]);

// If the user's `main` function expects arguments, the compiler will rename
// it to `__main_argc_argv`, and this version will get linked in, which
// initializes the argument data and calls `__main_argc_argv`.
//
// TODO: Ideally this function would be defined in a crt*.o file and linked in
// as necessary by the Clang driver.  However, moving it to crt1-command.c
// breaks `--no-gc-sections`, so we'll probably need to create a new file
// (e.g. crt0.o or crtend.o) and teach Clang to use it when needed.
__attribute__((__weak__, nodebug)) int __main_void(void) {
#if defined(__wasip1__)
  __wasi_errno_t err;

  // Get the sizes of the arrays we'll have to create to copy in the args.
  size_t argv_buf_size;
  size_t argc;
  err = __wasi_args_sizes_get(&argc, &argv_buf_size);
  if (err != __WASI_ERRNO_SUCCESS) {
    _Exit(EX_OSERR);
  }

  // Add 1 for the NULL pointer to mark the end, and check for overflow.
  size_t num_ptrs = argc + 1;
  if (num_ptrs == 0) {
    _Exit(EX_SOFTWARE);
  }

  // Allocate memory for storing the argument chars.
  char *argv_buf = malloc(argv_buf_size);
  if (argv_buf == NULL) {
    _Exit(EX_SOFTWARE);
  }

  // Allocate memory for the array of pointers. This uses `calloc` both to
  // handle overflow and to initialize the NULL pointer at the end.
  char **argv = calloc(num_ptrs, sizeof(char *));
  if (argv == NULL) {
    free(argv_buf);
    _Exit(EX_SOFTWARE);
  }

  // Fill the argument chars, and the argv array with pointers into those chars.
  // TODO: Remove the casts on `argv_ptrs` and `argv_buf` once the witx is
  // updated with char8 support.
  err = __wasi_args_get((uint8_t **)argv, (uint8_t *)argv_buf);
  if (err != __WASI_ERRNO_SUCCESS) {
    free(argv_buf);
    free(argv);
    _Exit(EX_OSERR);
  }

  // Call `__main_argc_argv` with the arguments!
  return __main_argc_argv(argc, argv);
#elif defined(__wasip2__) || defined(__wasip3__)
#ifdef __wasip2__
  typedef wasip2_list_string_t list_string_t;
  typedef wasip2_string_t string_t;
#define list_string_free wasip2_list_string_free
#else
  typedef wasip3_list_string_t list_string_t;
  typedef wasip3_string_t string_t;
#define list_string_free wasip3_list_string_free
#endif

  list_string_t argument_list;

  environment_get_arguments(&argument_list);

  // Add 1 for the NULL pointer to mark the end, and check for overflow.
  size_t argc = argument_list.len;
  size_t num_ptrs = argc + 1;
  if (num_ptrs == 0) {
    list_string_free(&argument_list);
    _Exit(EX_SOFTWARE);
  }

  // Allocate memory for the array of pointers. This uses `calloc` both to
  // handle overflow and to initialize the NULL pointer at the end.
  char **argv = calloc(num_ptrs, sizeof(char *));
  if (argv == NULL) {
    list_string_free(&argument_list);
    _Exit(EX_SOFTWARE);
  }

  // Copy the arguments
  for (size_t i = 0; i < argc; i++) {
    string_t wasi_string = argument_list.ptr[i];
    size_t len = wasi_string.len;
    argv[i] = malloc(len + 1);
    if (!argv[i]) {
      list_string_free(&argument_list);
      _Exit(EX_SOFTWARE);
    }
    memcpy(argv[i], wasi_string.ptr, len);
    argv[i][len] = '\0';
  }

  // Free the WASI argument list
  list_string_free(&argument_list);

  // Call `__main_argc_argv` with the arguments!
  return __main_argc_argv(argc, argv);
#elif defined(__webp2__)
  // See comments in crt1-reactor.c.
  if (!__main_argc_argv) {
    return 0;
  }
  static char *argv[] = {"./this.program", NULL};
  return __main_argc_argv(1, argv);
#else
#error "Unknown WASI version"
#endif
}
