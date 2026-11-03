#include <__function___isatty.h>
#include <errno.h>
#include <features.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>

#if !defined(__wasip1__)
#include <common/errors.h>
#include <stddefer.h>
#include <wasi/file_utils.h>
#endif

int __isatty(int fd) {
#if defined(__wasip1__)
  __wasi_fdstat_t statbuf;
  int r = __wasi_fd_fdstat_get(fd, &statbuf);
  if (r != 0) {
    errno = r;
    return 0;
  }

  // A tty is a character device that we can't seek or tell on.
  if (statbuf.fs_filetype != __WASI_FILETYPE_CHARACTER_DEVICE ||
      (statbuf.fs_rights_base &
       (__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL)) != 0) {
    errno = __WASI_ERRNO_NOTTY;
    return 0;
  }

  return 1;
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
  // Translate the file descriptor into an internal handle
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fd, &entry) < 0)
    return 0;
  defer descriptor_table_entry_dec(entry);
  if (!entry.vtable->isatty) {
    errno = ENOTTY;
    return 0;
  }

  return entry.vtable->isatty(entry.data);
#else
#error "Unsupported WASI version"
#endif
}

weak_alias(__isatty, isatty);
