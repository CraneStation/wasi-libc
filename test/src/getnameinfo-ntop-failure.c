#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>

const char *__wrap_inet_ntop(int af, const void *src, char *dst,
                             socklen_t size);
const char *__wrap_inet_ntop(int af, const void *src, char *dst,
                             socklen_t size) {
  (void)af;
  (void)src;
  (void)dst;
  (void)size;
  errno = ENOSPC;
  return NULL;
}

int main(void) {
  struct sockaddr_in addr = {.sin_family = AF_INET};
  char host[] = "host", serv[] = "port";

  assert(getnameinfo((const void *)&addr, sizeof(addr), host, sizeof(host),
                     serv, sizeof(serv),
                     NI_NUMERICHOST | NI_NUMERICSERV) == EAI_SYSTEM);
  assert(errno == ENOSPC);
  assert(memcmp(host, "host", sizeof(host)) == 0);
  assert(memcmp(serv, "port", sizeof(serv)) == 0);
}
