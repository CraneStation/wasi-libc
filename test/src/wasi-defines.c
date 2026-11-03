#include <wasi/version.h>

int main() {
#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__) ||       \
    defined(__webp2__)
  return 0;
#else
#error "Expected a wasi #define"
  return 1;
#endif
}
