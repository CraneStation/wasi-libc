#include <unistd.h>

int main(void) {
  // Write "abcd" to stdout using both the original handle and a dup.
  int d = dup(STDOUT_FILENO);
  if (d < 0) {
    return 1;
  }
  write(STDOUT_FILENO, "ab", 2);
  write(d, "cd", 2);
  write(STDOUT_FILENO, "\n", 1);
  close(d);
  return 0;
}
