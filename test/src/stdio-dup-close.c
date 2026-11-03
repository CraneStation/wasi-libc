#include <errno.h>
#include <unistd.h>

int main(void) {
  int d = dup(STDOUT_FILENO);
  if (d < 0) {
    return 1;
  }

  // Should print "hello" to stdout via the dup despite the original stdout
  // handle being closed.
  write(d, "hel", 3);
  if (close(STDOUT_FILENO) != 0) {
    write(d, "FAIL", 4);
  }
  if (write(STDOUT_FILENO, "X", 1) != -1 || errno != EBADF) {
    write(d, "FAIL", 4);
  }
  write(d, "lo", 2);
  close(d);

  return 0;
}
