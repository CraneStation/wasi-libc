#include "test.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

int main(void) {
  TEST(dup(-1) == -1 && errno == EBADF);
  TEST(dup(1234) == -1 && errno == EBADF);

  // A dup of stdout behaves like stdout.
  int d = dup(1);
  TEST(d > 2);
  TEST(write(d, "", 0) == 0);
  TEST((fcntl(d, F_GETFL) & O_ACCMODE) == O_WRONLY);
  struct stat st;
  TEST(fstat(d, &st) == 0);

  // A dup of stdin behaves like stdin.
  int r = dup(0);
  TEST(r > 2 && r != d);
  TEST((fcntl(r, F_GETFL) & O_ACCMODE) == O_RDONLY);
  TEST(close(r) == 0);

  TEST(dup2(1, 1) == 1);
  TEST(dup3(1, 1, 0) == -1 && errno == EINVAL);

  TEST(dup2(1, 40) == 40);
  TEST(write(40, "", 0) == 0);
  TEST(close(40) == 0);
  TEST(write(40, "", 0) == -1 && errno == EBADF);

  int f = fcntl(1, F_DUPFD, 20);
  TEST(f >= 20);
  TEST(write(f, "", 0) == 0);
  TEST(close(f) == 0);

  // Closing the dup leaves the original intact.
  TEST(close(d) == 0);
  TEST(write(d, "", 0) == -1 && errno == EBADF);
  TEST(write(1, "", 0) == 0);

  return t_status;
}
