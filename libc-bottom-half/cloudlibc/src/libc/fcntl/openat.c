// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <wasi/report-error.h>

#if defined(__wasip2__) || defined(__wasip3__)
#include <stddefer.h>
#include <wasi/file.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#ifdef __wasip1__
static_assert(O_APPEND == __WASI_FDFLAGS_APPEND, "Value mismatch");
static_assert(O_DSYNC == __WASI_FDFLAGS_DSYNC, "Value mismatch");
static_assert(O_NONBLOCK == __WASI_FDFLAGS_NONBLOCK, "Value mismatch");
static_assert(O_RSYNC == __WASI_FDFLAGS_RSYNC, "Value mismatch");
static_assert(O_SYNC == __WASI_FDFLAGS_SYNC, "Value mismatch");

static_assert(O_CREAT >> 12 == __WASI_OFLAGS_CREAT, "Value mismatch");
static_assert(O_DIRECTORY >> 12 == __WASI_OFLAGS_DIRECTORY, "Value mismatch");
static_assert(O_EXCL >> 12 == __WASI_OFLAGS_EXCL, "Value mismatch");
static_assert(O_TRUNC >> 12 == __WASI_OFLAGS_TRUNC, "Value mismatch");
#endif

int __wasilibc_nocwd_openat_nomode(int fd, const char *path, int oflag) {
#if defined(__wasip1__)
  // Compute rights corresponding with the access modes provided.
  // Attempt to obtain all rights, except the ones that contradict the
  // access mode provided to openat().
  __wasi_rights_t max =
      ~(__WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_READ |
        __WASI_RIGHTS_FD_WRITE | __WASI_RIGHTS_FD_ALLOCATE |
        __WASI_RIGHTS_FD_READDIR | __WASI_RIGHTS_FD_FILESTAT_SET_SIZE);
  switch (oflag & O_ACCMODE) {
    case O_RDONLY:
    case O_RDWR:
    case O_WRONLY:
      if ((oflag & O_RDONLY) != 0) {
        max |= __WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR;
      }
      if ((oflag & O_WRONLY) != 0) {
        max |= __WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_WRITE |
               __WASI_RIGHTS_FD_ALLOCATE |
               __WASI_RIGHTS_FD_FILESTAT_SET_SIZE;
      }
      break;
    case O_EXEC:
      break;
    case O_SEARCH:
      break;
    default:
      errno = EINVAL;
      return -1;
  }

  // Ensure that we can actually obtain the minimal rights needed.
  __wasi_fdstat_t fsb_cur;
  __wasi_errno_t error = __wasi_fd_fdstat_get(fd, &fsb_cur);
  if (error != 0) {
    errno = error;
    return -1;
  }

  // Path lookup properties.
  __wasi_lookupflags_t lookup_flags = 0;
  if ((oflag & O_NOFOLLOW) == 0)
    lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Open file with appropriate rights.
  __wasi_fdflags_t fs_flags = oflag & 0xfff;
  __wasi_rights_t fs_rights_base = max & fsb_cur.fs_rights_inheriting;
  __wasi_rights_t fs_rights_inheriting = fsb_cur.fs_rights_inheriting;
  __wasi_fd_t newfd;
  error = __wasi_path_open(fd, lookup_flags, path,
                                 (oflag >> 12) & 0xfff,
                                 fs_rights_base, fs_rights_inheriting, fs_flags,
                                 &newfd);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return newfd;
#elif defined(__wasip2__) || defined(__wasip3__)
  // Set up path flags
  filesystem_path_flags_t lookup_flags = 0;
  if ((oflag & O_NOFOLLOW) == 0)
    lookup_flags |= FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW;

  // Set up open flags
  filesystem_open_flags_t open_flags = 0;
  if ((oflag & O_CREAT) != 0)
      open_flags |= FILESYSTEM_OPEN_FLAGS_CREATE;
  if ((oflag & O_DIRECTORY) != 0)
      open_flags |= FILESYSTEM_OPEN_FLAGS_DIRECTORY;
  if ((oflag & O_EXCL) != 0)
      open_flags |= FILESYSTEM_OPEN_FLAGS_EXCLUSIVE;
  if ((oflag & O_TRUNC) != 0)
      open_flags |= FILESYSTEM_OPEN_FLAGS_TRUNCATE;

  // Set up descriptor flags
  filesystem_descriptor_flags_t fs_flags = 0;

  switch (oflag & O_ACCMODE) {
    case O_RDONLY:
    case O_RDWR:
    case O_WRONLY:
      if ((oflag & O_RDONLY) != 0) {
        fs_flags |= FILESYSTEM_DESCRIPTOR_FLAGS_READ;
      }
      if ((oflag & O_WRONLY) != 0) {
        fs_flags |= FILESYSTEM_DESCRIPTOR_FLAGS_WRITE | FILESYSTEM_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY;
      }
      break;
    case O_EXEC:
      break;
    case O_SEARCH:
      break;
    default:
      errno = EINVAL;
      return -1;
  }

  if ((oflag & O_SYNC) != 0)
    fs_flags |= FILESYSTEM_DESCRIPTOR_FLAGS_FILE_INTEGRITY_SYNC;
  if ((oflag & O_DSYNC) != 0)
    fs_flags |= FILESYSTEM_DESCRIPTOR_FLAGS_DATA_INTEGRITY_SYNC;
  if ((oflag & O_RSYNC) != 0)
    fs_flags |= FILESYSTEM_DESCRIPTOR_FLAGS_REQUESTED_WRITE_SYNC;

  // Translate the file descriptor to an internal handle
  filesystem_borrow_descriptor_t file_handle;
  descriptor_table_entry_t entry;
  if (fd_to_file_handle(fd, &entry, &file_handle) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);

  // Construct a WASI string for the path
  wasi_string_t wasi_path;
  if (wasi_string_from_c(path, &wasi_path) < 0)
    return -1;

  // Open the file, yielding a new handle
  filesystem_own_descriptor_t new_handle;
  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_open_at(file_handle,
                                                 lookup_flags,
                                                 &wasi_path,
                                                 open_flags,
                                                 fs_flags,
                                                 &new_handle,
                                                 &error_code);
  if (!ok) {
    translate_error(&error_code);
    return -1;
  }

  // Update the descriptor table with the new handle
  return __wasilibc_add_file(new_handle, oflag);
#elif defined(__webp2__)
  (void)fd;
  (void)path;
  WASILIBC_LOG_UNSUPPORTED("__wasilibc_nocwd_openat_nomode");
  errno = (oflag & O_CREAT) ? EACCES : ENOENT;
  return -1;
#else
# error "Unsupported WASI version"
#endif
}
