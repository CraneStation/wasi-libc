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
  if (size)
    dst[0] = 0;
  errno = ENOSPC;
  return NULL;
}

int main(void) {
  struct sockaddr_in addr = {.sin_family = AF_INET};
  char host[16] = "host", serv[8] = "port";
  int ret = getnameinfo((const void *)&addr, sizeof(addr), host, sizeof(host),
                        serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
  if (ret == 0)
    assert(strcmp(host, "0.0.0.0") == 0 && strcmp(serv, "0") == 0);
  else {
    assert(ret == EAI_SYSTEM && errno == ENOSPC);
    assert(memcmp(host, (char[16]){"host"}, sizeof(host)) == 0);
    assert(memcmp(serv, (char[8]){"port"}, sizeof(serv)) == 0);
  }
}
