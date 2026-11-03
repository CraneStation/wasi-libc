#include <stdlib.h>
#include <unistd.h>

int main(void) {
  int d = dup(STDOUT_FILENO);
  if (d < 0) {
    return 1;
  }

  // Even when the original stdout handle is closed, writes to stdout should be
  // flushed on exit.
  close(STDOUT_FILENO);
  write(d, "before-exit", 11);
  exit(0);
}
