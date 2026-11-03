#include <errno.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <common/errors.h>
#include <stddefer.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#endif

int __wasilibc_nocwd___wasilibc_unlinkat(int fd, const char *path) {
#if defined(__wasip1__)
  __wasi_errno_t error = __wasi_path_unlink_file(fd, path);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Create a Wasm string from the path
  wasi_string_t wasi_path;
  if (wasi_string_from_c(path, &wasi_path) < 0)
    return -1;

  // Unlink the file
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_unlink_file_at(file, &wasi_path,
                                                        &error_code);
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }

  return 0;
#elif defined(__webp2__)
  (void)fd;
  (void)path;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd___wasilibc_unlinkat");
  errno = ENOENT;
  return -1;
#else
#error "Unknown WASI version"
#endif
}
