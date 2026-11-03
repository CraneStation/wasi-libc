// Timed waits on deadlines that have already passed, including ones with a
// negative `tv_sec`, must time out immediately rather than block or spin.

#include "test.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

static void cond_timedwait(clockid_t clock, const struct timespec *deadline) {
  pthread_condattr_t attr;
  pthread_cond_t cond;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  TEST(pthread_condattr_init(&attr) == 0);
  TEST(pthread_condattr_setclock(&attr, clock) == 0);
  TEST(pthread_cond_init(&cond, &attr) == 0);
  TEST(pthread_mutex_lock(&mutex) == 0);
  TEST(pthread_cond_timedwait(&cond, &mutex, deadline) == ETIMEDOUT);
  TEST(pthread_mutex_unlock(&mutex) == 0);
  TEST(pthread_cond_destroy(&cond) == 0);
  TEST(pthread_condattr_destroy(&attr) == 0);
}

static void sem_timedwait_expired(const struct timespec *deadline) {
  sem_t sem;
  TEST(sem_init(&sem, 0, 0) == 0);
  errno = 0;
  TEST(sem_timedwait(&sem, deadline) == -1);
  TEST(errno == ETIMEDOUT);
  TEST(sem_destroy(&sem) == 0);
}

int main(void) {
  struct timespec negative = {.tv_sec = -1, .tv_nsec = 0};
  struct timespec negative_nsec = {.tv_sec = -1, .tv_nsec = 999999999};

  clockid_t clocks[] = {CLOCK_MONOTONIC, CLOCK_REALTIME};
  for (int i = 0; i < 2; i++) {
    cond_timedwait(clocks[i], &negative);
    cond_timedwait(clocks[i], &negative_nsec);

    // A deadline in the past with a positive `tv_sec`.
    struct timespec past;
    TEST(clock_gettime(clocks[i], &past) == 0);
    past.tv_sec -= 1;
    if (past.tv_sec >= 0)
      cond_timedwait(clocks[i], &past);
  }

  sem_timedwait_expired(&negative);
  sem_timedwait_expired(&negative_nsec);

  return t_status;
}
