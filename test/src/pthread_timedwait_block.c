#include "test.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#define TEST(c)                                                                \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

int main(void) {
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  struct timespec now;
  TEST(clock_gettime(CLOCK_REALTIME, &now) == 0);
  now.tv_nsec += 50 * 1000000;
  if (now.tv_nsec >= 1000000000) {
    now.tv_sec++;
    now.tv_nsec -= 1000000000;
  }
  TEST(pthread_mutex_lock(&mutex) == 0);
  TEST(pthread_cond_timedwait(&cond, &mutex, &now) == ETIMEDOUT);
  TEST(pthread_mutex_unlock(&mutex) == 0);
  return t_status;
}
