//! POSIX-like functions supporting absolute path arguments, implemented in
//! terms of `__wasilibc_find_relpath` and `*at`-style functions.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utime.h>
#include <wasi/libc-find-relpath.h>
#include <wasi/libc-nocwd.h>
#include <wasi/libc.h>
#include <wasi/report-error.h>

#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__)
static int find_relpath2(const char *path, char **relative,
                         size_t *relative_len) {
  // See comments in `preopens.c` for what this trick is doing.
  const char *abs;
  if (__wasilibc_find_relpath_alloc)
    return __wasilibc_find_relpath_alloc(path, &abs, relative, relative_len, 1);
  return __wasilibc_find_relpath(path, &abs, relative, *relative_len);
}

// Helper to call `__wasilibc_find_relpath` and return an already-managed
// pointer for the `relative` path. This function is not reentrant since the
// `relative` pointer will point to static data that cannot be reused until
// `relative` is no longer used.
static int find_relpath(const char *path, char **relative) {
  static __thread char *relative_buf = NULL;
  static __thread size_t relative_buf_len = 0;
  int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
  // find_relpath2 can update relative_buf, so assign it after the call
  *relative = relative_buf;
  return fd;
}

// same as `find_relpath`, but uses another set of static variables to cache
static int find_relpath_alt(const char *path, char **relative) {
  static __thread char *relative_buf = NULL;
  static __thread size_t relative_buf_len = 0;
  int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
  // find_relpath2 can update relative_buf, so assign it after the call
  *relative = relative_buf;
  return fd;
}
#elif defined(__webp2__)
// For webp2 we always return -1, which will always take these functions down
// the "no preopen" path, which means in just about every case we know we can
// just log that this function is unsupported.
static int find_relpath(const char *path, char **relative) {
  (void)path;
  (void)relative;
  return -1;
}
static int find_relpath_alt(const char *path, char **relative) {
  (void)path;
  (void)relative;
  return -1;
}
#endif

int open(const char *path, int oflag, ...) {
  // WASI libc's `openat` ignores the mode argument, so call a special
  // entrypoint which avoids the varargs calling convention.
  return __wasilibc_open_nomode(path, oflag);
}

// See the documentation in libc.h
int __wasilibc_open_nomode(const char *path, int oflag) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_open_nomode");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_openat_nomode(dirfd, relative_path, oflag);
}

int access(const char *path, int amode) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("access");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_faccessat(dirfd, relative_path, amode, 0);
}

ssize_t readlink(const char *restrict path, char *restrict buf,
                 size_t bufsize) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("readlink");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_readlinkat(dirfd, relative_path, buf, bufsize);
}

int stat(const char *restrict path, struct stat *restrict buf) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("stat");
#endif

  if (path[0] == '\0') {
    errno = ENOENT;
    return -1;
  }

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_fstatat(dirfd, relative_path, buf, 0);
}

int lstat(const char *restrict path, struct stat *restrict buf) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("lstat");
#endif

  if (path[0] == '\0') {
    errno = ENOENT;
    return -1;
  }

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_fstatat(dirfd, relative_path, buf,
                                  AT_SYMLINK_NOFOLLOW);
}

int utime(const char *path, const struct utimbuf *times) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("utime");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_utimensat(
      dirfd, relative_path,
      times ? ((struct timespec[2]){{.tv_sec = times->actime},
                                    {.tv_sec = times->modtime}})
            : NULL,
      0);
}

int utimes(const char *path, const struct timeval times[2]) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("utimes");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_utimensat(
      dirfd, relative_path,
      times
          ? ((struct timespec[2]){
                {.tv_sec = times[0].tv_sec, .tv_nsec = times[0].tv_usec * 1000},
                {.tv_sec = times[1].tv_sec, .tv_nsec = times[1].tv_usec * 1000},
            })
          : NULL,
      0);
}

int unlink(const char *path) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("unlink");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  // `unlinkat` imports `__wasi_path_remove_directory` even when
  // `AT_REMOVEDIR` isn't passed. Instead, use a specialized function which
  // just imports `__wasi_path_unlink_file`.
  return __wasilibc_nocwd___wasilibc_unlinkat(dirfd, relative_path);
}

int rmdir(const char *path) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("rmdir");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd___wasilibc_rmdirat(dirfd, relative_path);
}

int remove(const char *path) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("remove");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  // First try to remove it as a file.
  int r = __wasilibc_nocwd___wasilibc_unlinkat(dirfd, relative_path);
  if (r != 0 && (errno == EISDIR || errno == ENOENT || errno == EACCES ||
                 errno == EPERM)) {
    // That failed, but it might be a directory.
    r = __wasilibc_nocwd___wasilibc_rmdirat(dirfd, relative_path);

    // If it isn't a directory, we lack capabilities to remove it as a file.
    if (errno == ENOTDIR)
      errno = ENOENT;
  }
  return r;
}

int mkdir(const char *path, mode_t mode) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("mkdir");
#endif

  (void)mode;
  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_mkdirat_nomode(dirfd, relative_path);
}

DIR *opendir(const char *dirname) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("opendir");
#endif

  char *relative_path;
  int dirfd = find_relpath(dirname, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return NULL;
  }

  return __wasilibc_nocwd_opendirat(dirfd, relative_path);
}

int scandir(const char *restrict dir, struct dirent ***restrict namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("scandir");
#endif

  char *relative_path;
  int dirfd = find_relpath(dir, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_scandirat(dirfd, relative_path, namelist, filter,
                                    compar);
}

int symlink(const char *target, const char *linkpath) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("symlink");
#endif

  char *relative_path;
  int dirfd = find_relpath(linkpath, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_symlinkat(target, dirfd, relative_path);
}

int link(const char *old, const char *new) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("link");
#endif

  char *old_relative_path;
  int old_dirfd = find_relpath_alt(old, &old_relative_path);

  if (old_dirfd != -1) {
    char *new_relative_path;
    int new_dirfd = find_relpath(new, &new_relative_path);

    if (new_dirfd != -1)
      return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path, new_dirfd,
                                     new_relative_path, 0);
  }

  // We couldn't find a preopen for it; fail as if we can't find the path.
  errno = ENOENT;
  return -1;
}

int rename(const char *old, const char *new) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("rename");
#endif

  char *old_relative_path;
  int old_dirfd = find_relpath_alt(old, &old_relative_path);

  if (old_dirfd != -1) {
    char *new_relative_path;
    int new_dirfd = find_relpath(new, &new_relative_path);

    if (new_dirfd != -1)
      return __wasilibc_nocwd_renameat(old_dirfd, old_relative_path, new_dirfd,
                                       new_relative_path);
  }

  // We couldn't find a preopen for it; fail as if we can't find the path.
  errno = ENOENT;
  return -1;
}

int chmod(const char *path, mode_t mode) {
  (void)path;
  (void)mode;
  // TODO: We plan to support this eventually in WASI, but not yet.
  // Meanwhile, we provide a stub so that libc++'s `<filesystem>`
  // implementation will build unmodified.
  WASILIBC_LOG_UNSUPPORTED("chmod");
  errno = ENOSYS;
  return -1;
}

int fchmod(int fd, mode_t mode) {
  (void)fd;
  (void)mode;
  // TODO: We plan to support this eventually in WASI, but not yet.
  // Meanwhile, we provide a stub so that libc++'s `<filesystem>`
  // implementation will build unmodified.
  WASILIBC_LOG_UNSUPPORTED("fchmod");
  errno = ENOSYS;
  return -1;
}

int fchmodat(int fd, const char *path, mode_t mode, int flag) {
  (void)fd;
  (void)path;
  (void)mode;
  (void)flag;
  // TODO: We plan to support this eventually in WASI, but not yet.
  // Meanwhile, we provide a stub so that libc++'s `<filesystem>`
  // implementation will build unmodified.
  WASILIBC_LOG_UNSUPPORTED("fchmodat");
  errno = ENOSYS;
  return -1;
}

int statvfs(const char *__restrict path, struct statvfs *__restrict buf) {
  (void)path;
  (void)buf;
  // TODO: We plan to support this eventually in WASI, but not yet.
  // Meanwhile, we provide a stub so that libc++'s `<filesystem>`
  // implementation will build unmodified.
  WASILIBC_LOG_UNSUPPORTED("statvfs");
  errno = ENOSYS;
  return -1;
}

int fstatvfs(int fd, struct statvfs *buf) {
  (void)fd;
  (void)buf;
  // TODO: We plan to support this eventually in WASI, but not yet.
  // Meanwhile, we provide a stub so that libc++'s `<filesystem>`
  // implementation will build unmodified.
  WASILIBC_LOG_UNSUPPORTED("fstatvfs");
  errno = ENOSYS;
  return -1;
}

// Like `access`, but with `faccessat`'s flags argument.
int __wasilibc_access(const char *path, int mode, int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_access");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_faccessat(dirfd, relative_path, mode, flags);
}

// Like `utimensat`, but without the `at` part.
int __wasilibc_utimens(const char *path, const struct timespec times[2],
                       int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__waslibc_utimens");
#endif

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_utimensat(dirfd, relative_path, times, flags);
}

// Like `stat`, but with `fstatat`'s flags argument.
int __wasilibc_stat(const char *__restrict path, struct stat *__restrict st,
                    int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_stat");
#endif

  if (path[0] == '\0') {
    errno = ENOENT;
    return -1;
  }

  char *relative_path;
  int dirfd = find_relpath(path, &relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_fstatat(dirfd, relative_path, st, flags);
}

// Like `link`, but with `linkat`'s flags argument.
int __wasilibc_link(const char *oldpath, const char *newpath, int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_link");
#endif

  char *old_relative_path;
  char *new_relative_path;
  int old_dirfd = find_relpath(oldpath, &old_relative_path);
  int new_dirfd = find_relpath_alt(newpath, &new_relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (old_dirfd == -1 || new_dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path, new_dirfd,
                                 new_relative_path, flags);
}

// Like `__wasilibc_link`, but oldpath is relative to olddirfd.
int __wasilibc_link_oldat(int olddirfd, const char *oldpath,
                          const char *newpath, int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_link_oldat");
#endif

  char *new_relative_path;
  int new_dirfd = find_relpath(newpath, &new_relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (new_dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_linkat(olddirfd, oldpath, new_dirfd,
                                 new_relative_path, flags);
}

// Like `__wasilibc_link`, but newpath is relative to newdirfd.
int __wasilibc_link_newat(const char *oldpath, int newdirfd,
                          const char *newpath, int flags) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_link_newat");
#endif

  char *old_relative_path;
  int old_dirfd = find_relpath(oldpath, &old_relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (old_dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path, newdirfd,
                                 newpath, flags);
}

// Like `rename`, but from is relative to fromdirfd.
int __wasilibc_rename_oldat(int fromdirfd, const char *from, const char *to) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_rename_oldat");
#endif

  char *to_relative_path;
  int to_dirfd = find_relpath(to, &to_relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (to_dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_renameat(fromdirfd, from, to_dirfd, to_relative_path);
}

// Like `rename`, but to is relative to todirfd.
int __wasilibc_rename_newat(const char *from, int todirfd, const char *to) {
#ifdef __webp2__
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_rename_newat");
#endif

  char *from_relative_path;
  int from_dirfd = find_relpath(from, &from_relative_path);

  // If we can't find a preopen for it, fail as if we can't find the path.
  if (from_dirfd == -1) {
    errno = ENOENT;
    return -1;
  }

  return __wasilibc_nocwd_renameat(from_dirfd, from_relative_path, todirfd, to);
}
