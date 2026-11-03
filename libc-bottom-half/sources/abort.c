#include <stdlib.h>
#include <wasi/webp2_stdio.h>

void abort(void) {
  // wasm doesn't support signals, so just trap to halt the program.
#ifdef __webp2__
  __wasilibc_webp2_flush_and_close_stdio(/*also_musl=*/true);
#endif
  __builtin_trap();
}
