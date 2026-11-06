//! realpath() under a preopen mounted below the root.
//!
//! The test's directory is preopened at /work/app and nothing is preopened
//! at "/" (see test/CMakeLists.txt), the way a sandbox mounts one project
//! directory. "/work" belongs to no preopen, so a walk that asks the host
//! about every prefix of a path fails before it reaches /work/app. The tree
//! goes in a directory of its own under the preopen, named at random, so
//! the static and shared builds of this test can run at the same time.

#include "test.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

#define ROOT "/work/app"

static char base[64];

// `base` + `rel` (`rel` empty or starting with '/').
static const char *at(const char *rel) {
  static char bufs[4][PATH_MAX];
  static int next;
  char *buf = bufs[next++ % 4];
  snprintf(buf, PATH_MAX, "%s%s", base, rel);
  return buf;
}

static void expect(const char *path, const char *want) {
  errno = 0;
  char *got = realpath(path, NULL);
  if (!got) {
    t_error("realpath(\"%s\") failed (errno %d, %s), want \"%s\"\n", path,
            errno, strerror(errno), want);
    return;
  }
  if (strcmp(got, want) != 0)
    t_error("realpath(\"%s\") = \"%s\", want \"%s\"\n", path, got, want);
  free(got);
}

static void expect_err(const char *path, int want) {
  errno = 0;
  char *got = realpath(path, NULL);
  if (got) {
    t_error("realpath(\"%s\") = \"%s\", want errno %d (%s)\n", path, got, want,
            strerror(want));
    free(got);
    return;
  }
  if (errno != want)
    t_error("realpath(\"%s\") failed with errno %d (%s), want %d (%s)\n", path,
            errno, strerror(errno), want, strerror(want));
}

int main(void) {
  unsigned char id[6];
  TEST(getentropy(id, sizeof id) == 0);
  snprintf(base, sizeof base, ROOT "/t-%02x%02x%02x%02x%02x%02x", id[0], id[1],
           id[2], id[3], id[4], id[5]);

  // base/dir/{file.txt, sub/, up -> .., filelink -> sub/../file.txt}
  // base/{link -> dir, loop -> loop, dangling -> missing,
  //       abslink -> <base>/dir}
  int fd;
  TEST(mkdir(base, 0755) == 0);
  TEST(mkdir(at("/dir"), 0755) == 0);
  TEST(mkdir(at("/dir/sub"), 0755) == 0);
  TEST((fd = open(at("/dir/file.txt"), O_CREAT | O_WRONLY, 0644)) >= 0);
  TEST(close(fd) == 0);
  // Some hosts cannot create every symlink form used below (for
  // example, directory or dangling symlinks on Windows). Skip the
  // assertions for a link the host would not create.
  int dirlink = symlink("dir", at("/link")) == 0;
  int uplink = symlink("..", at("/dir/up")) == 0;
  int filelink = symlink("sub/../file.txt", at("/dir/filelink")) == 0;
  int looplink = symlink("loop", at("/loop")) == 0;
  int dangling = symlink("missing", at("/dangling")) == 0;
  // A host may refuse a symlink to an absolute path.
  int abslink = symlink(at("/dir"), at("/abslink")) == 0;

  // The preopen, and what lies under it.
  expect(ROOT, ROOT);
  expect(ROOT "/", ROOT);
  expect(base, base);
  expect(at("/dir"), at("/dir"));
  expect(at("/dir/file.txt"), at("/dir/file.txt"));
  expect(at("//dir/./sub/"), at("/dir/sub"));
  // Symlinks resolve, and before a ".." that follows them.
  if (dirlink) {
    expect(at("/link"), at("/dir"));
    expect(at("/link/file.txt"), at("/dir/file.txt"));
    expect(at("/link/../dir/sub"), at("/dir/sub"));
    expect(at("/link/sub/.."), at("/dir"));
  }
  if (uplink)
    expect(at("/dir/up/dir/file.txt"), at("/dir/file.txt"));
  if (filelink)
    expect(at("/dir/filelink"), at("/dir/file.txt"));
  expect(at("/dir/sub/../file.txt"), at("/dir/file.txt"));
  if (abslink)
    expect(at("/abslink/sub"), at("/dir/sub"));
  if (abslink && dirlink)
    expect(at("/abslink/../link"), at("/dir"));
  // Above the preopen: directories, which the host is not asked about.
  expect(ROOT "/..", "/work");
  expect("/work", "/work");
  expect("/work/", "/work");
  expect("/", "/");
  expect("/..", "/");
  expect(ROOT "/../app", ROOT);
  expect(ROOT "/../../work/./app", ROOT);

  // Errors, as natively.
  expect_err(at("/missing"), ENOENT);
  expect_err(at("/missing/.."), ENOENT);
  if (dangling)
    expect_err(at("/dangling"), ENOENT);
  if (looplink)
    expect_err(at("/loop"), ELOOP);
  expect_err(at("/dir/file.txt/"), ENOTDIR);
  expect_err(at("/dir/file.txt/."), ENOTDIR);
  expect_err(at("/dir/file.txt/.."), ENOTDIR);
  expect_err(at("/dir/file.txt/x"), ENOTDIR);
  if (filelink)
    expect_err(at("/dir/filelink/"), ENOTDIR);
  expect_err("", ENOENT);
  // Paths no preopen reaches.
  expect_err("/elsewhere", ENOENT);
  expect_err("/work/other", ENOENT);
  expect_err("/work/app2", ENOENT);

  // Relative paths resolve from the cwd.
  TEST(chdir(at("/dir")) == 0);
  expect(".", at("/dir"));
  expect("file.txt", at("/dir/file.txt"));
  expect("./sub/../file.txt", at("/dir/file.txt"));
  expect("..", base);
  if (dirlink)
    expect("../link/sub", at("/dir/sub"));
  expect("../../..", "/work");
  expect_err("nope", ENOENT);

  // Into the caller's buffer.
  char buf[PATH_MAX];
  TEST(realpath("sub", buf) == buf);
  if (strcmp(buf, at("/dir/sub")) != 0)
    t_error("realpath(\"sub\", buf) = \"%s\", want \"%s\"\n", buf,
            at("/dir/sub"));

  // Leave the preopen as it was.
  TEST(chdir(ROOT) == 0);
  unlink(at("/abslink"));
  if (dangling)
    TEST(unlink(at("/dangling")) == 0);
  if (looplink)
    TEST(unlink(at("/loop")) == 0);
  if (dirlink)
    TEST(unlink(at("/link")) == 0);
  if (filelink)
    TEST(unlink(at("/dir/filelink")) == 0);
  if (uplink)
    TEST(unlink(at("/dir/up")) == 0);
  TEST(unlink(at("/dir/file.txt")) == 0);
  TEST(rmdir(at("/dir/sub")) == 0);
  TEST(rmdir(at("/dir")) == 0);
  TEST(rmdir(base) == 0);
  return t_status;
}
