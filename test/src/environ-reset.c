#include <stdlib.h>
#include <string.h>
#include <wasi/libc-environ.h>

#include "test.h"

extern char **environ;

int main(void) {
  char borrowed[] = "borrowed=value";
  char *replacement[] = {borrowed, NULL};
  for (int i = 0; i < 262144; i++) {
    const char *value = getenv("WASI_ENV_TEST");
    const char *empty = getenv("WASI_ENV_EMPTY");
    if (value == NULL || strcmp(value, "original") != 0 || empty == NULL ||
        *empty != '\0' || setenv("WASI_ENV_TEST", "changed", 1) != 0 ||
        putenv(borrowed) != 0) {
      t_error("environment reload or update failed\n");
      return t_status;
    }
    if (i % 2 == 0) {
      clearenv();
      environ = replacement;
    }
    __wasilibc_deinitialize_environ();
  }
  if (strcmp(borrowed, "borrowed=value") != 0)
    t_error("environment reset changed caller-owned storage\n");
  return t_status;
}
