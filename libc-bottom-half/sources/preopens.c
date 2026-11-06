//! Support for "preopens", file descriptors passed into the program from the
//! environment, with associated path prefixes, which can be used to map
//! absolute paths to capabilities with relative paths.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <lock.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wasi/api.h>
#include <wasi/libc-find-relpath.h>
#include <wasi/libc.h>

#ifndef __wasip1__
#include <wasi/descriptor_table.h>
#include <wasi/file.h>
#include <wasi/file_utils.h>
#endif

/// Access to the the above preopen must be protected in the presence of
/// threads.
DECLARE_STRONG_LOCK(lock, static);

#if defined(__wasip1__)
typedef struct {
  __wasi_fd_t fd;
} preopen_state_t;

typedef __wasi_fd_t preopen_t;

static void assert_preopen_valid(preopen_t fd) {
  (void)fd;
  assert(fd != AT_FDCWD);
  assert(fd != -1);
}

#ifndef NDEBUG
static void preopen_state_assert_open(const preopen_state_t *state) {
  assert(state->fd != -1);
}
#endif

static int preopen_state_initialize(preopen_state_t *state, preopen_t fd) {
  state->fd = fd;
  return 0;
}

static int preopen_state_get_fd(const preopen_state_t *state) {
  return state->fd;
}

static void preopen_state_close(preopen_state_t *state) { (void)state; }

#elif defined(__wasip2__) || defined(__wasip3__)

typedef struct {
  int libc_fd;
} preopen_state_t;

typedef filesystem_preopens_own_descriptor_t preopen_t;

static void assert_preopen_valid(preopen_t fd) {
  (void)fd;
  assert(fd.__handle != 0);
}

#ifndef NDEBUG
static void preopen_state_assert_open(const preopen_state_t *state) {
  assert(state->libc_fd != -1);
}
#endif

static int preopen_state_initialize(preopen_state_t *state, preopen_t fd) {
  state->libc_fd = __wasilibc_add_file(fd, O_RDWR);
  if (state->libc_fd < 0) {
    filesystem_descriptor_drop_own(fd);
    return -1;
  }
  return 0;
}

static int preopen_state_get_fd(const preopen_state_t *state) {
  return state->libc_fd;
}

static void preopen_state_close(preopen_state_t *state) {
  close(state->libc_fd);
}
#else
#error "Unknown WASI version"
#endif

/// A name and file descriptor pair.
typedef struct preopen {
  /// The path prefix associated with the file descriptor.
  char *prefix;

  preopen_state_t state;
} preopen;

/// A simple growable array of `preopen`.
static _Atomic _Bool preopens_populated = false;
static preopen *preopens;
static size_t num_preopens;
static size_t preopen_capacity;

#ifdef NDEBUG
#define assert_invariants() // assertions disabled
#else
static void assert_invariants(void) {
  assert(num_preopens <= preopen_capacity);
  assert(preopen_capacity == 0 || preopens != NULL);
  assert(preopen_capacity == 0 ||
         preopen_capacity * sizeof(preopen) > preopen_capacity);

  for (size_t i = 0; i < num_preopens; ++i) {
    const preopen *pre = &preopens[i];
    assert(pre->prefix != NULL);
    preopen_state_assert_open(&pre->state);
#ifdef __wasm__
    assert((uintptr_t)pre->prefix <
           (__uint128_t)__builtin_wasm_memory_size(0) * PAGESIZE);
#endif
  }
}
#endif

/// Allocate space for more preopens. Returns 0 on success and -1 on failure.
static int resize(void) {
  size_t start_capacity = 4;
  size_t old_capacity = preopen_capacity;
  size_t new_capacity = old_capacity == 0 ? start_capacity : old_capacity * 2;

  preopen *old_preopens = preopens;
  preopen *new_preopens = calloc(sizeof(preopen), new_capacity);
  if (new_preopens == NULL) {
    return -1;
  }

  memcpy(new_preopens, old_preopens, num_preopens * sizeof(preopen));
  preopens = new_preopens;
  preopen_capacity = new_capacity;
  free(old_preopens);

  assert_invariants();
  return 0;
}

// Normalize an absolute path. Removes leading `/` and leading `./`, so the
// first character is the start of a directory name. This works because our
// process always starts with a working directory of `/`. Additionally translate
// `.` to the empty string.
static const char *strip_prefixes(const char *path) {
  while (1) {
    if (path[0] == '/') {
      path++;
    } else if (path[0] == '.' && path[1] == '/') {
      path += 2;
    } else if (path[0] == '.' && path[1] == 0) {
      path++;
    } else {
      break;
    }
  }

  return path;
}

/// Similar to `internal_register_preopened_fd` but does not take a lock.
static int internal_register_preopened_fd_unlocked(preopen_t fd,
                                                   const char *relprefix) {
  // Check preconditions.
  assert_invariants();
  assert_preopen_valid(fd);
  assert(relprefix != NULL);

  if (num_preopens == preopen_capacity && resize() != 0)
    goto err;

  char *prefix = strdup(strip_prefixes(relprefix));
  if (prefix == NULL)
    goto err;
  preopens[num_preopens].prefix = prefix;
  if (preopen_state_initialize(&preopens[num_preopens].state, fd) < 0)
    goto err_free_prefix;
  num_preopens++;

  assert_invariants();
  return 0;

err_free_prefix:
  free(prefix);
err:
  return -1;
}

/// Are the `prefix_len` bytes pointed to by `prefix` a prefix of `path`?
static bool prefix_matches(const char *prefix, size_t prefix_len,
                           const char *path) {
  // Allow an empty string as a prefix of any relative path.
  if (path[0] != '/' && prefix_len == 0)
    return true;

  // Check whether any bytes of the prefix differ.
  if (memcmp(path, prefix, prefix_len) != 0)
    return false;

  // Ignore trailing slashes in directory names.
  size_t i = prefix_len;
  while (i > 0 && prefix[i - 1] == '/') {
    --i;
  }

  // Match only complete path components.
  char last = path[i];
  return last == '/' || last == '\0';
}

#ifdef __wasip1__
/// Register the given preopened file descriptor under the given path.
///
/// This function takes ownership of `prefix`.
static int internal_register_preopened_fd(__wasi_fd_t fd,
                                          const char *relprefix) {
  STRONG_LOCK(lock);

  int r = internal_register_preopened_fd_unlocked(fd, relprefix);

  STRONG_UNLOCK(lock);

  return r;
}

// See the documentation in libc.h
int __wasilibc_register_preopened_fd(int fd, const char *prefix) {
  __wasilibc_populate_preopens();

  return internal_register_preopened_fd((__wasi_fd_t)fd, prefix);
}
#endif

// See the documentation in libc-find-relpath.h.
int __wasilibc_find_relpath(const char *path, const char **abs_prefix,
                            char **relative_path, size_t relative_path_len) {
  // If `chdir` is linked, whose object file defines this symbol, then we
  // call that. Otherwise if the program can't `chdir` then `path` is
  // absolute (or relative to the root dir), so we delegate to `find_abspath`
  if (__wasilibc_find_relpath_alloc)
    return __wasilibc_find_relpath_alloc(path, abs_prefix, relative_path,
                                         &relative_path_len, 0);
  return __wasilibc_find_abspath(path, abs_prefix,
                                 (const char **)relative_path);
}

// See the documentation in libc-find-relpath.h.
int __wasilibc_find_abspath(const char *path, const char **abs_prefix,
                            const char **relative_path) {
  __wasilibc_populate_preopens();

  // Strip leading `/` characters, the prefixes we're matching won't have
  // them.
  while (*path == '/')
    path++;
  // Search through the preopens table. Iterate in reverse so that more
  // recently added preopens take precedence over less recently addded ones.
  size_t match_len = 0;
  int fd = -1;
  STRONG_LOCK(lock);
  for (size_t i = num_preopens; i > 0; --i) {
    const preopen *pre = &preopens[i - 1];
    const char *prefix = pre->prefix;
    size_t len = strlen(prefix);

    // If we haven't had a match yet, or the candidate path is longer than
    // our current best match's path, and the candidate path is a prefix of
    // the requested path, take that as the new best path.
    if ((fd == -1 || len > match_len) && prefix_matches(prefix, len, path)) {
      fd = preopen_state_get_fd(&pre->state);
      match_len = len;
      *abs_prefix = prefix;
    }
  }
  STRONG_UNLOCK(lock);

  if (fd == -1) {
    errno = ENOENT;
    return -1;
  }

  // The relative path is the substring after the portion that was matched.
  const char *computed = path + match_len;

  // Omit leading slashes in the relative path.
  while (*computed == '/')
    ++computed;

  // *at syscalls don't accept empty relative paths, so use "." instead.
  if (*computed == '\0')
    computed = ".";

  *relative_path = computed;
  return fd;
}

/// Where the absolute path `path` (`len` bytes, slash-separated, with no `.`
/// or `..` component) stands among the preopens, for `realpath` (see
/// libc-top-half/musl/src/misc/realpath.c):
///
///  * 2 - an exact preopen mount, or an ancestor with no broader preopen.
///    These are synthetic directories: the host may refuse readlink on a
///    mount itself and no host descriptor reaches an inaccessible ancestor.
///  * 1 - below a preopen, unless the path is itself an exact mount. A
///    nested mount does not hide a symlink reachable through a broader one.
///  * 0 - no preopen reaches the path: the program has no such file.
__attribute__((__visibility__("hidden"))) int
__wasilibc_preopen_kind(const char *path, size_t len);

#define PREOPEN_OUTSIDE 0
#define PREOPEN_INSIDE 1
#define PREOPEN_MOUNT 2

int __wasilibc_preopen_kind(const char *path, size_t len) {
  __wasilibc_populate_preopens();

  // Compare without the leading `/`, as the prefixes are stored.
  while (len > 0 && *path == '/') {
    path++;
    len--;
  }
  // The root holds every preopen.
  if (len == 0)
    return PREOPEN_MOUNT;

  int kind = PREOPEN_OUTSIDE;
  int ancestor = 0;
  STRONG_LOCK(lock);
  for (size_t i = 0; i < num_preopens; ++i) {
    const char *prefix = preopens[i].prefix;
    size_t prefix_len = strlen(prefix);
    // Ignore trailing slashes in directory names.
    while (prefix_len > 0 && prefix[prefix_len - 1] == '/')
      --prefix_len;

    if (prefix_len >= len) {
      if (memcmp(prefix, path, len) == 0) {
        if (prefix_len == len) {
          // An exact mount remains a directory even under a broader mount.
          kind = PREOPEN_MOUNT;
          break;
        }
        if (prefix[len] == '/')
          ancestor = 1;
      }
    } else if (prefix_len == 0 || (memcmp(prefix, path, prefix_len) == 0 &&
                                   path[prefix_len] == '/')) {
      // Accessible ancestors must be resolved through the host. Keep
      // looking only for an exact mount, which takes precedence.
      kind = PREOPEN_INSIDE;
    }
  }
  STRONG_UNLOCK(lock);
  return kind == PREOPEN_OUTSIDE && ancestor ? PREOPEN_MOUNT : kind;
}

void __wasilibc_populate_preopens(void) {
  // Fast path: If the preopens are already initialized, do nothing.
  if (preopens_populated) {
    return;
  }

  STRONG_LOCK(lock);

  // Check whether another thread initialized the preopens already.
  if (preopens_populated) {
    STRONG_UNLOCK(lock);
    return;
  }

#if defined(__wasip1__)
  // Skip stdin, stdout, and stderr, and count up until we reach an invalid
  // file descriptor.
  for (__wasi_fd_t fd = 3; fd != 0; ++fd) {
    __wasi_prestat_t prestat;
    __wasi_errno_t ret = __wasi_fd_prestat_get(fd, &prestat);
    if (ret == __WASI_ERRNO_BADF)
      break;
    if (ret != __WASI_ERRNO_SUCCESS)
      goto oserr;
    switch (prestat.tag) {
    case __WASI_PREOPENTYPE_DIR: {
      char *prefix = malloc(prestat.u.dir.pr_name_len + 1);
      if (prefix == NULL)
        goto software;

      // TODO: Remove the cast on `prefix` once the witx is updated with
      // char8 support.
      ret = __wasi_fd_prestat_dir_name(fd, (uint8_t *)prefix,
                                       prestat.u.dir.pr_name_len);
      if (ret != __WASI_ERRNO_SUCCESS)
        goto oserr;
      prefix[prestat.u.dir.pr_name_len] = '\0';

      int r = internal_register_preopened_fd_unlocked(fd, prefix);
      free(prefix);
      if (r != 0)
        goto software;

      break;
    }
    default:
      break;
    }
  }
#elif defined(__wasip2__) || defined(__wasip3__)
  filesystem_preopens_list_tuple2_own_descriptor_string_t preopens;
  filesystem_preopens_get_directories(&preopens);

  for (size_t i = 0; i < preopens.len; ++i) {
    filesystem_preopens_tuple2_own_descriptor_string_t name_and_descriptor =
        preopens.ptr[i];
    char *prefix = strndup((const char *)name_and_descriptor.f1.ptr,
                           name_and_descriptor.f1.len);
#ifdef __wasip2__
    wasip2_string_free(&name_and_descriptor.f1);
#else
    wasip3_string_free(&name_and_descriptor.f1);
#endif
    if (prefix == NULL) {
      filesystem_descriptor_drop_own(name_and_descriptor.f0);
      goto software;
    }
    int r =
        internal_register_preopened_fd_unlocked(name_and_descriptor.f0, prefix);
    free(prefix);
    if (r != 0)
      goto software;
  }

#else
#error "Unknown WASI version"
#endif

  // Preopens are now initialized.
  preopens_populated = true;

  STRONG_UNLOCK(lock);

  return;
#ifdef __wasip1__
oserr:
  _Exit(EX_OSERR);
#endif
software:
  _Exit(EX_SOFTWARE);
}

void __wasilibc_reset_preopens(void) {
  STRONG_LOCK(lock);

  if (num_preopens) {
    for (unsigned i = 0; i < num_preopens; ++i) {
      free((void *)preopens[i].prefix);
      preopen_state_close(&preopens[i].state);
    }
    free(preopens);
  }

  preopens_populated = false;
  preopens = NULL;
  num_preopens = 0;
  preopen_capacity = 0;

  assert_invariants();

#ifndef __wasip1__
  descriptor_table_clear();
#endif

  STRONG_UNLOCK(lock);
}
