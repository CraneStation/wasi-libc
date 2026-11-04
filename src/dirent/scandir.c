#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

static int wrapper_cmp(const void *v1, const void *v2, void *cmp)
{
	const struct dirent *de1 = *(struct dirent *const *){v1};
	const struct dirent *de2 = *(struct dirent *const *){v2};
	return ((int (*)(const struct dirent **, const struct dirent **))cmp)(
		&de1, &de2);
}

int scandir(const char *path, struct dirent ***res,
	int (*sel)(const struct dirent *),
	int (*cmp)(const struct dirent **, const struct dirent **))
{
	/* opendir() and closedir() are cancellation points. scandir() is also
	 * allowed to be a cancellation point but we choose not to make it one.
	 * To avoid calling sel() and cmp() with altered thread state,
	 * cancellation is not explicitly disabled for those calls. This means
	 * if either of the callbacks acts upon a cancellation request, there
	 * can be memory and file descriptor leaks. */
	int cs;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	DIR *d = opendir(path);
	pthread_setcancelstate(cs, 0);

	struct dirent *de, **names=0, **tmp;
	size_t cnt=0, len=0;
	int old_errno = errno;

	if (!d) return -1;

	while ((errno=0), (de = readdir(d))) {
		if (sel) {
			/* sel() must not observe that errno was set to 0. */
			errno = old_errno;
			if (!sel(de)) continue;
		}
		if (cnt >= INT_MAX) {
			errno = EOVERFLOW;
			break;
		}
		if (cnt >= len) {
			len = 2*len+1;
			if (len > SIZE_MAX/sizeof *names) {
				errno = ENOMEM;
				break;
			}
			tmp = realloc(names, len * sizeof *names);
			if (!tmp) break;
			names = tmp;
		}
		names[cnt] = malloc(de->d_reclen);
		if (!names[cnt]) break;
		memcpy(names[cnt++], de, de->d_reclen);
	}
	/* closedir() might set errno via __aio_close(). It might also "fail"
	 * (return -1 and set errno). But even then, the file descriptor is
	 * closed and memory is freed, so there is no reason to report the
	 * "failure" of closedir() as a failure of scandir(). */
	int err = errno;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	closedir(d);
	pthread_setcancelstate(cs, 0);

	if (err) {
		if (names) while (cnt-->0) free(names[cnt]);
		free(names);
		errno = err;
		return -1;
	}
	/* cmp() and caller must not observe that errno was set to 0. */
	errno = old_errno;

	if (cmp) __qsort_r(names, cnt, sizeof *names, wrapper_cmp, (void *)cmp);
	*res = names;
	return cnt;
}
