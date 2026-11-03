// Copyright (c) 2015 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <dirent.h>
#include <errno.h>
#include <wasi/report-error.h>

#include "dirent_impl.h"

long telldir(DIR *dirp) {
#if defined(__wasip1__)
  return dirp->cookie;
#elif defined(__wasip2__) || defined(__wasip3__)
  return dirp->offset + dirp->skip;
#elif defined(__webp2__)
  (void)dirp;
  WASILIBC_LOG_UNSUPPORTED("telldir");
  errno = ENOSYS;
  return -1;
#else
# error "Unsupported WASI version"
#endif
}
