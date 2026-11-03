#include "test.h"
#include <caller.h>

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

int main() {
  caller_subtask_status_t status = test_simple_i_blocking();
  TEST(CALLER_SUBTASK_STATE(status) == CALLER_SUBTASK_STARTED);
  caller_subtask_t subtask = CALLER_SUBTASK_HANDLE(status);

  test_simple_i_barge_in();

  // For wasip2 this `main` function is a synchronous entrypoint so we can't
  // cancel the task which would block, but for wasip3 and beyond it's ok to
  // block so it's ok to cancel.
#if defined(__wasip2__) || defined(__webp2__)
  (void)subtask;
#else
  status = caller_subtask_cancel(subtask);
  TEST(CALLER_SUBTASK_STATE(status) == CALLER_SUBTASK_RETURNED);
#endif

  return t_status + test_simple_i_t_status();
}
