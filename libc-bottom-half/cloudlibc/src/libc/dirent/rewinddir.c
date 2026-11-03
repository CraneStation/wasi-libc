// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <dirent.h>
#include <errno.h>
#include <wasi/api.h>
#include <wasi/report-error.h>

#include "dirent_impl.h"

void rewinddir(DIR *dirp) {
#if defined(__wasip1__)
  // Update cookie.
  dirp->cookie = __WASI_DIRCOOKIE_START;
  // Mark entire buffer as processed to force a read of new data.
  dirp->buffer_used = dirp->buffer_processed = dirp->buffer_size;
#elif defined(__wasip2__) || defined(__wasip3__)
  dirent_close_streams(dirp);
  dirp->skip = 0;
  dirp->offset = 0;
#elif defined(__webp2__)
  (void)dirp;
  WASILIBC_LOG_UNSUPPORTED("rewinddir");
  errno = ENOSYS;
#else
# error "Unknown WASI version"
#endif
}
