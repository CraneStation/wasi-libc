#include "stdio_impl.h"
#include <stdlib.h>

static void dummy(FILE *f) { }
weak_alias(dummy, __unlist_locked_file);

int fclose(FILE *f)
{
	int r;
	FILE **head = 0;

	r = fflush(f);
	if (f->pipe_pid) head = __ofl_lock();
	r |= f->close(f);

	/* Past this point, f is closed and any further explict access
	 * to it is undefined. However, it still exists as an entry in
	 * the open file list and possibly in the thread's locked files
	 * list, if it was closed while explicitly locked. Functions
	 * which process these lists must tolerate dead FILE objects
	 * (which necessarily have inactive buffer pointers) without
	 * producing any side effects. */

	if (f->flags & F_PERM) return r;

	__unlist_locked_file(f);

	if (!head) head = __ofl_lock();
	if (f->prev) f->prev->next = f->next;
	if (f->next) f->next->prev = f->prev;
	if (*head == f) *head = f->next;
	__ofl_unlock();

	free(f->getln_buf);
	free(f);

	return r;
}
