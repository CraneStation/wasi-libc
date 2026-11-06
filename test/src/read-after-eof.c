#include "test.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// Reads `expected` from `fd`, whose current offset is at the end of what was
// read so far.
static void expect_read(int fd, const char *expected, const char *what) {
  char buf[16] = {0};
  errno = 0;
  ssize_t n = read(fd, buf, sizeof(buf));
  size_t len = strlen(expected);
  if (n != (ssize_t)len || memcmp(buf, expected, len) != 0)
    t_error("%s: expected %zu bytes \"%s\", got %zd bytes \"%.*s\" "
            "(errno=%d)\n",
            what, len, expected, n, n > 0 ? (int)n : 0, buf, errno);
}

static void expect_ok(int result, const char *what) {
  if (result < 0)
    t_error("%s failed: errno=%d\n", what, errno);
}

int main(void) {
  const char *path = "read-after-eof.tmp";
  int writer = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (writer == -1) {
    t_error("open for writing failed: errno=%d\n", errno);
    return t_status;
  }
  int reader = open(path, O_RDONLY);
  if (reader == -1) {
    t_error("open for reading failed: errno=%d\n", errno);
    close(writer);
    unlink(path);
    return t_status;
  }

  expect_read(reader, "", "read of the empty file");

  // Data appended after a read reached the end is visible to the next read,
  // as when following a growing log file.
  expect_ok(write(writer, "one", 3), "first write");
  expect_read(reader, "one", "read after the file grew");
  expect_read(reader, "", "read at the new end");

  // Seeking to the current offset reads the file again, which is how SQLite
  // reads a page that another connection wrote.
  expect_ok(write(writer, "two", 3), "second write");
  expect_ok(lseek(reader, 3, SEEK_SET), "lseek to the current offset");
  expect_read(reader, "two", "read after lseek to the current offset");

  expect_ok(lseek(reader, 0, SEEK_SET), "lseek to the start");
  expect_read(reader, "onetwo", "read after lseek to the start");

  expect_ok(close(reader), "close of the reader");
  expect_ok(close(writer), "close of the writer");
  expect_ok(unlink(path), "unlink");
  return t_status;
}
