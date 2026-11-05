#include <fcntl.h>
#include <unistd.h>
#include <wasi/libc.h>

#include "test.h"

int main(void) {
  // Repeated initialization must reuse memory and preserve directory access.
  for (int i = 0; i < 262144; i++) {
    __wasilibc_populate_preopens();
    __wasilibc_reset_preopens();
  }
  int fd = open(".", O_RDONLY | O_DIRECTORY);
  if (fd < 0)
    t_error("cannot open the preopened directory\n");
  else if (close(fd) != 0)
    t_error("cannot close the directory\n");
  return t_status;
}
