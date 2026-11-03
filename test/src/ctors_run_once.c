#undef NDEBUG

#include <assert.h>

// Static constructors must run exactly once, including when the test is
// dynamically linked against `libc.so`.

static int ctor_count;

__attribute__((constructor)) static void ctor(void) { ctor_count++; }

int main() {
  assert(ctor_count == 1);
  return 0;
}
