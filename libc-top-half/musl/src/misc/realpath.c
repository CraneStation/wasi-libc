#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#ifndef __wasilibc_unmodified_upstream
#include <sys/stat.h>
#endif

#ifdef __wasilibc_unmodified_upstream

static size_t slash_len(const char *s)
{
	const char *s0 = s;
	while (*s == '/') s++;
	return s-s0;
}

char *realpath(const char *restrict filename, char *restrict resolved)
{
	char stack[PATH_MAX+1];
	char output[PATH_MAX];
	size_t p, q, l, l0, cnt=0, nup=0;
	int check_dir=0;

	if (!filename) {
		errno = EINVAL;
		return 0;
	}
	l = strnlen(filename, sizeof stack);
	if (!l) {
		errno = ENOENT;
		return 0;
	}
	if (l >= PATH_MAX) goto toolong;
	p = sizeof stack - l - 1;
	q = 0;
	memcpy(stack+p, filename, l+1);

	/* Main loop. Each iteration pops the next part from stack of
	 * remaining path components and consumes any slashes that follow.
	 * If not a link, it's moved to output; if a link, contents are
	 * pushed to the stack. */
restart:
	for (; ; p+=slash_len(stack+p)) {
		/* If stack starts with /, the whole component is / or //
		 * and the output state must be reset. */
		if (stack[p] == '/') {
			check_dir=0;
			nup=0;
			q=0;
			output[q++] = '/';
			p++;
			/* Initial // is special. */
			if (stack[p] == '/' && stack[p+1] != '/')
				output[q++] = '/';
			continue;
		}

		char *z = __strchrnul(stack+p, '/');
		l0 = l = z-(stack+p);

		if (!l && !check_dir) break;

		/* Skip any . component but preserve check_dir status. */
		if (l==1 && stack[p]=='.') {
			p += l;
			continue;
		}

		/* Copy next component onto output at least temporarily, to
		 * call readlink, but wait to advance output position until
		 * determining it's not a link. */
		if (q && output[q-1] != '/') {
			if (!p) goto toolong;
			stack[--p] = '/';
			l++;
		}
		if (q+l >= PATH_MAX) goto toolong;
		memcpy(output+q, stack+p, l);
		output[q+l] = 0;
		p += l;

		int up = 0;
		if (l0==2 && stack[p-2]=='.' && stack[p-1]=='.') {
			up = 1;
			/* Any non-.. path components we could cancel start
			 * after nup repetitions of the 3-byte string "../";
			 * if there are none, accumulate .. components to
			 * later apply to cwd, if needed. */
			if (q <= 3*nup) {
				nup++;
				q += l;
				continue;
			}
			/* When previous components are already known to be
			 * directories, processing .. can skip readlink. */
			if (!check_dir) goto skip_readlink;
		}
		ssize_t k = readlink(output, stack, p);
		if (k==p) goto toolong;
		if (!k) {
			errno = ENOENT;
			return 0;
		}
		if (k<0) {
			if (errno != EINVAL) return 0;
skip_readlink:
			check_dir = 0;
			if (up) {
				while(q && output[q-1]!='/') q--;
				if (q>1 && (q>2 || output[0]!='/')) q--;
				continue;
			}
			if (l0) q += l;
			check_dir = stack[p];
			continue;
		}
		if (++cnt == SYMLOOP_MAX) {
			errno = ELOOP;
			return 0;
		}

		/* If link contents end in /, strip any slashes already on
		 * stack to avoid /->// or //->/// or spurious toolong. */
		if (stack[k-1]=='/') while (stack[p]=='/') p++;
		p -= k;
		memmove(stack+p, stack, k);

		/* Skip the stack advancement in case we have a new
		 * absolute base path. */
		goto restart;
	}

 	output[q] = 0;

	if (output[0] != '/') {
		if (!getcwd(stack, sizeof stack)) return 0;
		l = strlen(stack);
		/* Cancel any initial .. components. */
		p = 0;
		while (nup--) {
			while(l>1 && stack[l-1]!='/') l--;
			if (l>1) l--;
			p += 2;
			if (p<q) p++;
		}
		if (q-p && stack[l-1]!='/') stack[l++] = '/';
		if (l + (q-p) + 1 >= PATH_MAX) goto toolong;
		memmove(output + l, output + p, q - p + 1);
		memcpy(output, stack, l);
		q = l + q-p;
	}

	if (resolved) return memcpy(resolved, output, q+1);
	else return strdup(output);

toolong:
	errno = ENAMETOOLONG;
	return 0;
}
#else
/* WASI: a program reaches files only through its preopens, so a path is
 * resolved among them. Upstream's walk asks readlink about every prefix of
 * the path, starting at the first component. Under a preopen mounted below
 * the root (say "/work/app") the components above it ("/work") belong to no
 * preopen, so readlink fails with ENOENT, and a host may refuse readlink on
 * the preopen itself (ENOTCAPABLE). Then no path under that preopen
 * resolves at all.
 *
 * Here a relative path is joined to the cwd first, and each prefix of the
 * resulting absolute path is placed among the preopens
 * (__wasilibc_preopen_kind): the root, a preopen's own path and ancestors
 * unreachable through any broader preopen are synthetic directories;
 * below a preopen, even above a nested one, the host is asked (lstat, then
 * readlink for a symlink), so symlinks resolve as they do natively and a
 * symlink to an absolute path starts the walk again at the root; a path no
 * preopen reaches does not exist (ENOENT, as for open). ".." takes the last
 * component off the result, which holds no symlink, so the walk never asks
 * the host about a path above a preopen. A component that a slash follows
 * must be a directory (ENOTDIR otherwise, from what lstat said, not from
 * whatever error a host gives for a path through a file). */

__attribute__((__visibility__("hidden"))) int
__wasilibc_preopen_kind(const char *path, size_t len);

#define PREOPEN_OUTSIDE 0
#define PREOPEN_INSIDE 1
#define PREOPEN_MOUNT 2

char *realpath(const char *restrict filename, char *restrict resolved)
{
	char stack[PATH_MAX+1];
	char output[PATH_MAX];
	size_t p, q, q0, l, cnt=0;
	int dir;
	struct stat st;

	if (!filename) {
		errno = EINVAL;
		return 0;
	}
	l = strnlen(filename, sizeof stack);
	if (!l) {
		errno = ENOENT;
		return 0;
	}
	if (l >= PATH_MAX) goto toolong;
	p = sizeof stack - l - 1;
	memcpy(stack+p, filename, l+1);

	/* A relative path names what the cwd joined with it names. */
	if (stack[p] != '/') {
		if (!getcwd(output, sizeof output)) return 0;
		l = strlen(output);
		if (l+1 > p) goto toolong;
		stack[--p] = '/';
		p -= l;
		memcpy(stack+p, output, l);
	}

	/* The stack holds what is left to resolve, from stack+p on. It starts
	 * with a slash here and after a symlink whose contents are absolute:
	 * the walk starts again at the root. output (q bytes) is resolved and
	 * holds no symlink; dir says whether it is a directory. */
restart:
	q = 0;
	output[q++] = '/';
	dir = 1;
	for (;;) {
		while (stack[p] == '/') p++;
		if (!stack[p]) break;
		l = strcspn(stack+p, "/");

		if (l==1 && stack[p]=='.') {
			p += l;
			continue;
		}
		if (l==2 && stack[p]=='.' && stack[p+1]=='.') {
			while (q>1 && output[q-1]!='/') q--;
			if (q>1) q--;
			p += l;
			dir = 1;
			continue;
		}

		/* Put the component on output, to keep it unless it is a
		 * symlink. */
		q0 = q;
		if (q>1) {
			if (q+1 >= PATH_MAX) goto toolong;
			output[q++] = '/';
		}
		if (q+l >= PATH_MAX) goto toolong;
		memcpy(output+q, stack+p, l);
		q += l;
		output[q] = 0;
		p += l;

		switch (__wasilibc_preopen_kind(output, q)) {
		case PREOPEN_MOUNT:
			dir = 1;
			break;
		case PREOPEN_INSIDE:
			if (lstat(output, &st)) return 0;
			if (S_ISLNK(st.st_mode)) {
				ssize_t k = readlink(output, stack, p);
				if (k == (ssize_t)p) goto toolong;
				if (!k) {
					errno = ENOENT;
					return 0;
				}
				if (k < 0) return 0;
				if (++cnt == SYMLOOP_MAX) {
					errno = ELOOP;
					return 0;
				}
				/* The link's contents go in front of what is
				 * left, resolved from the link's directory
				 * (output as it was, a directory) or, when
				 * absolute, from the root. */
				p -= k;
				memmove(stack+p, stack, k);
				q = q0;
				if (stack[p] == '/') goto restart;
				continue;
			}
			dir = S_ISDIR(st.st_mode);
			break;
		default:
			errno = ENOENT;
			return 0;
		}

		/* A slash after a component, before more of the path or at its
		 * end, says that it is a directory. */
		if (stack[p] == '/' && !dir) {
			errno = ENOTDIR;
			return 0;
		}
	}

	output[q] = 0;
	if (resolved) return memcpy(resolved, output, q+1);
	else return strdup(output);

toolong:
	errno = ENAMETOOLONG;
	return 0;
}
#endif
