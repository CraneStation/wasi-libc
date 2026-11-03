#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <wasi/report-error.h>

#include "lock.h"

#ifndef __webp2__
char *__wasilibc_cwd = "/";
DECLARE_WEAK_LOCK(__wasilibc_cwd_lock);
#endif

char *getcwd(char *buf, size_t size) {
#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__)
  // Critical section contains no yield points, so we can use weak locks.
  WEAK_LOCK(__wasilibc_cwd_lock);
  if (!buf) {
    buf = strdup(__wasilibc_cwd);
    if (!buf) {
      errno = ENOMEM;
      WEAK_UNLOCK(__wasilibc_cwd_lock);
      return NULL;
    }
  } else {
    size_t len = strlen(__wasilibc_cwd);
    if (size < len + 1) {
      errno = ERANGE;
      WEAK_UNLOCK(__wasilibc_cwd_lock);
      return NULL;
    }
    strcpy(buf, __wasilibc_cwd);
  }
  WEAK_UNLOCK(__wasilibc_cwd_lock);
  return buf;
#elif defined(__webp2__)
  (void)buf;
  (void)size;
  WASILIBC_LOG_UNSUPPORTED("getcwd");
  errno = ENOENT;
  return NULL;
#else
#error "Unknown WASI version"
#endif
}
