#include "test.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <wasi/version.h>

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

int main(void) {
  char tmp[] = "testsuite-XXXXXX";
  int fd;
  int nbytes = 0;
  int flags = 0;
  flags |= O_RDWR | O_CREAT | O_EXCL;

  TEST((fd = open(tmp, flags, 0600)) > 2);
  TEST(write(fd, "hello", 6) == 6);

#if defined(__wasip1__)
  // Always returns 0?
  TEST(ioctl(fd, FIONREAD, &nbytes) == 0);
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Not supported on wasip{2,3}
  TEST(ioctl(fd, FIONREAD, &nbytes) == -1);
#else
#error "Unknown WASI version"
#endif
  close(fd);
  TEST(unlink(tmp) == 0);

  return t_status;
}
