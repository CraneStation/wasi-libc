// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int __wasilibc_nocwd_scandirat(int dirfd, const char *dir, struct dirent ***res,
                               int (*sel)(const struct dirent *),
                               int (*cmp)(const struct dirent **, const struct dirent **)) {
        dirfd = openat(dirfd, dir, O_RDONLY | O_DIRECTORY);
        if (dirfd < 0) {
            return -1;
        }
        // Note that below this is a copy of `scandir.c` located in the top-half
        // of this library.
	DIR *d = fdopendir(dirfd);
	struct dirent *de, **names=0, **tmp;
	size_t cnt=0, len=0;
	int old_errno = errno;

	if (!d) {
		int saved_errno = errno;
		close(dirfd);
		errno = saved_errno;
		return -1;
	}

	while ((errno=0), (de = readdir(d))) {
		if (sel && !sel(de)) continue;
		if (cnt >= len) {
			len = 2*len+1;
			if (len > SIZE_MAX/sizeof *names) break;
			tmp = realloc(names, len * sizeof *names);
			if (!tmp) break;
			names = tmp;
		}
                size_t size = sizeof(struct dirent) + strlen(de->d_name) + 1;
		names[cnt] = malloc(size);
		if (!names[cnt]) break;
		memcpy(names[cnt++], de, size);
	}

	closedir(d);

	if (errno) {
		if (names) while (cnt-->0) free(names[cnt]);
		free(names);
		return -1;
	}
	errno = old_errno;

	if (cmp) qsort(names, cnt, sizeof *names, (int (*)(const void *, const void *))cmp);
	*res = names;
	return cnt;

}
