// Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

#include "dirent_impl.h"

#ifdef __wasip1__
static_assert(DT_BLK == __WASI_FILETYPE_BLOCK_DEVICE, "Value mismatch");
static_assert(DT_CHR == __WASI_FILETYPE_CHARACTER_DEVICE, "Value mismatch");
static_assert(DT_DIR == __WASI_FILETYPE_DIRECTORY, "Value mismatch");
static_assert(DT_FIFO == __WASI_FILETYPE_SOCKET_STREAM, "Value mismatch");
static_assert(DT_LNK == __WASI_FILETYPE_SYMBOLIC_LINK, "Value mismatch");
static_assert(DT_REG == __WASI_FILETYPE_REGULAR_FILE, "Value mismatch");
static_assert(DT_UNKNOWN == __WASI_FILETYPE_UNKNOWN, "Value mismatch");
#endif

#ifdef __wasip3__
#include <wasi/wasip3_block.h>
#endif

#ifndef __webp2__
// Grows a buffer to be large enough to hold a certain amount of data.
static struct dirent* grow(struct dirent **buffer, size_t *buffer_size, size_t target_size) {
  if (*buffer_size < target_size) {
    size_t new_size = *buffer_size;
    while (new_size < target_size)
      new_size *= 2;
    void *new_buffer = realloc(*buffer, new_size);
    if (new_buffer == NULL) {
      errno = ENOMEM;
      return NULL;
    }
    *buffer = new_buffer;
    *buffer_size = new_size;
  }
  return *buffer;
}
#endif

#if defined(__wasip1__)
struct dirent *readdir(DIR *dirp) {
  for (;;) {
    // Extract the next dirent header.
    size_t buffer_left = dirp->buffer_used - dirp->buffer_processed;
    if (buffer_left < sizeof(__wasi_dirent_t)) {
      // End-of-file.
      if (dirp->buffer_used < dirp->buffer_size)
        return NULL;
      goto read_entries;
    }
    __wasi_dirent_t entry;
    memcpy(&entry, dirp->buffer + dirp->buffer_processed, sizeof(entry));

    size_t entry_size = sizeof(__wasi_dirent_t) + entry.d_namlen;
    if (entry.d_namlen == 0) {
      // Invalid pathname length. Skip the entry.
      dirp->buffer_processed += entry_size;
      continue;
    }

    // The entire entry must be present in buffer space. If not, read
    // the entry another time. Ensure that the read buffer is large
    // enough to fit at least this single entry.
    if (buffer_left < entry_size) {
      if (grow((struct dirent**) &dirp->buffer, &dirp->buffer_size, entry_size) == NULL)
        return NULL;
      goto read_entries;
    }

    // Skip entries having null bytes in the filename.
    const char *name = dirp->buffer + dirp->buffer_processed + sizeof(entry);
    if (memchr(name, '\0', entry.d_namlen) != NULL) {
      dirp->buffer_processed += entry_size;
      continue;
    }

    // Return the next directory entry. Ensure that the dirent is large
    // enough to fit the filename.
    if (grow(&dirp->dirent, &dirp->dirent_size,
         offsetof(struct dirent, d_name) + entry.d_namlen + 1) == NULL)
      return NULL;
    struct dirent *dirent = dirp->dirent;
    dirent->d_type = entry.d_type;
    memcpy(dirent->d_name, name, entry.d_namlen);
    dirent->d_name[entry.d_namlen] = '\0';

    // `fd_readdir` implementations may set the inode field to zero if the
    // the inode number is unknown. In that case, do an `fstatat` to get the
    // inode number.
    off_t d_ino = entry.d_ino;
    unsigned char d_type = entry.d_type;
    if (d_ino == 0 && strcmp(dirent->d_name, "..") != 0) {
      struct stat statbuf;
      if (fstatat(dirp->fd, dirent->d_name, &statbuf, AT_SYMLINK_NOFOLLOW) != 0) {
	if (errno == ENOENT) {
	  // The file disappeared before we could read it, so skip it.
	  dirp->buffer_processed += entry_size;
	  continue;
	}
        return NULL;
      }

      // Fill in the inode.
      d_ino = statbuf.st_ino;

      // In case someone raced with us and replaced the object with this name
      // with another of a different type, update the type too.
      d_type = __wasilibc_iftodt(statbuf.st_mode & S_IFMT);
    }
    dirent->d_ino = d_ino;
    dirent->d_type = d_type;

    dirp->cookie = entry.d_next;
    dirp->buffer_processed += entry_size;
    return dirent;

  read_entries:
    // Discard data currently stored in the input buffer.
    dirp->buffer_used = dirp->buffer_processed = dirp->buffer_size;

    // Load more directory entries and continue.
    __wasi_errno_t error =
        // TODO: Remove the cast on `dirp->buffer` once the witx is updated with char8 support.
        __wasi_fd_readdir(dirp->fd, (uint8_t *)dirp->buffer, dirp->buffer_size,
                                  dirp->cookie, &dirp->buffer_used);
    if (error != 0) {
      errno = error;
      return NULL;
    }
    dirp->buffer_processed = 0;
  }
}

#elif defined(__wasip2__) || defined(__wasip3__)
// Ensures that `dirp` has the necessary streams prepared to start reading
// directory entries.
//
// On success returns `entry`, the owned entry for this operation that must be
// deallocated with `descriptor_table_entry_dec` when done, and `handle`, the
// WASI file that's being used. Additionally `dirp`'s streams are filled and
// ready for use.
//
// On failure returns -1 and `entry` need not be deallocated.
static int ensure_has_directory_stream(DIR *dirp,
                                       descriptor_table_entry_t *entry,
                                       filesystem_borrow_descriptor_t *handle) {
  if (fd_to_file_handle(dirp->fd, entry, handle) < 0)
    return -1;

#ifdef __wasip2__
  if (dirp->stream.__handle != 0)
    return 0;

  filesystem_error_code_t error_code;
  bool ok = filesystem_method_descriptor_read_directory(*handle,
                                                        &dirp->stream,
                                                        &error_code);
  if (!ok) {
    translate_error(&error_code);
    descriptor_table_entry_dec(*entry);
    return -1;
  }
#elif defined(__wasip3__)
  if (dirp->stream.f0 == 0)
    filesystem_method_descriptor_read_directory(*handle, &dirp->stream);
#endif
  return 0;
}

static struct dirent *readdir_next(DIR *dirp) {
  bool ok;
  filesystem_metadata_hash_value_t metadata;
  filesystem_error_code_t error_code;
  filesystem_borrow_descriptor_t dir_handle;
  descriptor_table_entry_t entry;

  if (ensure_has_directory_stream(dirp, &entry, &dir_handle) < 0)
    return NULL;
  defer descriptor_table_entry_dec(entry);

  // Yield '.' first if the offset is 0. Note that `d_ino` is from the metadata
  // hash of the directory itself.
  if (dirp->offset == 0) {
    dirp->offset += 1;
    if (grow(&dirp->dirent, &dirp->dirent_size, offsetof(struct dirent, d_name) + 2) == NULL)
      return NULL;
    ok = filesystem_method_descriptor_metadata_hash(dir_handle,
                                                    &metadata,
                                                    &error_code);
    if (!ok) {
      translate_error(&error_code);
      return NULL;
    }
    dirp->dirent->d_ino = metadata.lower;
    dirp->dirent->d_type = DT_DIR;
    dirp->dirent->d_name[0] = '.';
    dirp->dirent->d_name[1] = 0;
    return dirp->dirent;
  }

  // Yield '..' next if the offset is 1. Note that `d_ino` is set to 0 to
  // avoid opening the parent directory here.
  if (dirp->offset == 1) {
    dirp->offset += 1;
    if (grow(&dirp->dirent, &dirp->dirent_size, offsetof(struct dirent, d_name) + 3) == NULL)
      return NULL;
    dirp->dirent->d_ino = 0;
    dirp->dirent->d_type = DT_DIR;
    dirp->dirent->d_name[0] = '.';
    dirp->dirent->d_name[1] = '.';
    dirp->dirent->d_name[2] = 0;
    return dirp->dirent;
  }

#if defined(__wasip2__)
  filesystem_borrow_directory_entry_stream_t stream = filesystem_borrow_directory_entry_stream(dirp->stream);
  filesystem_option_directory_entry_t dir_entry_optional;
  ok = filesystem_method_directory_entry_stream_read_directory_entry(stream,
                                                                     &dir_entry_optional,
                                                                     &error_code);
  if (!ok) {
    translate_error(&error_code);
    return NULL;
  }

  // Reached end-of-directory? Return null.
  if (!dir_entry_optional.is_some)
    return NULL;

  filesystem_directory_entry_t dir_entry = dir_entry_optional.val;

#elif defined(__wasip3__)
  filesystem_directory_entry_t dir_entry;

  // Don't try to keep reading once the stream is closed.
  if (dirp->stream_done)
    return NULL;

  // Loop until at least one stream entry is read, or until the stream is closed.
  while (!dirp->stream_done) {
    size_t amount =
      __wasilibc_stream_block_on(
          filesystem_stream_directory_entry_read(dirp->stream.f0, &dir_entry, 1),
          dirp->stream.f0,
          &dirp->stream_done);

    // If something was read, then break out and process that below.
    if (amount > 0)
      break;

    // If nothing was read and the stream isn't finished yet, try again.
    if (!dirp->stream_done)
      continue;

    // If the stream's result future hasn't been read yet, do so here.
    if (dirp->stream.f1) {
      filesystem_result_void_error_code_t result;
      __wasilibc_future_block_on(
          filesystem_future_result_void_error_code_read(dirp->stream.f1, &result),
          dirp->stream.f1);
      filesystem_future_result_void_error_code_drop_readable(dirp->stream.f1);
      dirp->stream.f1 = 0;
      if (result.is_err)
        translate_error(&result.val.err);
    }

    // The stream is closed, so return NULL. If `errno` needs to be set it'll
    // have been done above with `f1`.
    return NULL;
  }
#else
#error "Unknown WASI version"
#endif

  // Ensure that the dirent is large enough to fit the filename
  size_t the_size = offsetof(struct dirent, d_name);
  if (grow(&dirp->dirent, &dirp->dirent_size, the_size + dir_entry.name.len + 1) == NULL) {
    filesystem_directory_entry_free(&dir_entry);
    return NULL;
  }

  // Fill out `d_type` and `d_name`
  dirp->dirent->d_type = dir_entry_type_to_d_type(&dir_entry.type);
  memcpy(dirp->dirent->d_name, dir_entry.name.ptr, dir_entry.name.len);
  dirp->dirent->d_name[dir_entry.name.len] = '\0';

  // Fill out `d_ino` with the metadata hash.
  filesystem_path_flags_t path_flags = 0; // Don't follow symlinks
  ok = filesystem_method_descriptor_metadata_hash_at(dir_handle,
                                                     path_flags,
                                                     &dir_entry.name,
                                                     &metadata,
                                                     &error_code);
  filesystem_directory_entry_free(&dir_entry);
  if (!ok) {
    translate_error(&error_code);
    return NULL;
  }
  dirp->dirent->d_ino = metadata.lower;
  dirp->offset += 1;

  return dirp->dirent;
}

struct dirent *readdir(DIR *dirp) {
  struct dirent *result = readdir_next(dirp);
  while (result != NULL && dirp->skip > 0) {
    dirp->skip -= 1;
    result = readdir_next(dirp);
  }
  return result;
}
#elif defined(__webp2__)
struct dirent *readdir(DIR *dirp) {
  (void)dirp;
  WASILIBC_LOG_UNSUPPORTED("readdir");
  errno = EBADF; // It is impossible to open a DIR anyway.
  return NULL;
}
#else
# error "Unknown WASI version"
#endif
