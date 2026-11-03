This is a list of toolchain-specific quirks, to be migrated to better user-facing documentation in the future.

## Web Preview 2 (webp2)

- Some libc functionality is not implemented and will trap if called:
  - Files and file descriptors (with exceptions to support stdio below)
  - Sockets
  - Sleeping
- `stdout` and `stderr` are mapped to `console.log` and `console.warn` respectively:
  - Output is expected to be UTF-8. Invalid UTF-8 will be replaced with U+FFFD (�).
  - Console output is buffered until a newline is reached. `fflush` can be used to trigger a `console.log` or `console.warn` at any time, regardless of newlines.
- `stdin` is readable but will always EOF.
- `environ` is always empty.
- All calls to `exit` will trap, including `exit(0)`.
  - This is because there is currently no way to abort execution of a WebAssembly program without trapping.
- The `main` function has additional restrictions:
  - `argc` is always 1, and `argv` is always `{"./this.program"}`.
  - In order for exported functions to remain callable after `main`, no destructors are run when `main` finishes.
  - If `main` returns a non-zero value, the program will trap, analogous to `exit`.
