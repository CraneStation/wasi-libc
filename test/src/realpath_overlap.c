//! A nested preopen must not hide symlinks reachable from a broader one.
//! The harness preopens a filesystem at / and another directory at
//! /work/app/canon/link/sub. The latter's ancestors still need lstat.
#include "test.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void expect(const char *path, const char *want) {
  char *got = realpath(path, NULL);
  if (!got || strcmp(got, want))
    t_error("realpath(%s) = %s, errno %d, want %s\n", path, got ? got : "NULL",
            errno, want);
  free(got);
}

int main(void) {
  const char *dirs[] = {"/work", "/work/app", "/work/app/canon",
                        "/work/app/canon/dir", "/work/app/canon/dir/sub"};
  for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; ++i)
    if (mkdir(dirs[i], 0755))
      t_error("mkdir(%s): %s\n", dirs[i], strerror(errno));
  // Some hosts cannot create directory symlinks (for example, on
  // Windows). There is nothing to check here without the symlink.
  if (symlink("dir", "/work/app/canon/link") == 0) {
    expect("/work/app/canon/link", "/work/app/canon/dir");
    expect("/work/app/canon/link/sub", "/work/app/canon/dir/sub");
    expect("/work/app/canon/link/sub/..", "/work/app/canon/dir");
  }
  return t_status;
}
