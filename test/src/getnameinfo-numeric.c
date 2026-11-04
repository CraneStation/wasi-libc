#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

static void check_host(const char *host, const char *expected, int family) {
  char text[2][80], *scope[2];
  unsigned char address[2][16];
  assert(memchr(host, 0, sizeof(text[0])));
  strcpy(text[0], host);
  strcpy(text[1], expected);
  for (int i = 0; i < 2; i++) {
    scope[i] = strchr(text[i], '%');
    if (scope[i])
      *scope[i]++ = 0;
    assert(inet_pton(family, text[i], address[i]) == 1);
  }
  assert(scope[0] ? scope[1] && strcmp(scope[0], scope[1]) == 0 : !scope[1]);
  assert(memcmp(address[0], address[1], family == AF_INET ? 4 : 16) == 0);
}

static void check_nameinfo(const struct sockaddr *sa, socklen_t len, int flags,
                           const char *expected_host,
                           const char *expected_serv) {
  char host[80], serv[32];
  assert(getnameinfo(sa, len, host, sizeof(host), serv, sizeof(serv), flags) ==
         0);
  check_host(host, expected_host, sa->sa_family);
  assert(strcmp(serv, expected_serv) == 0);
  size_t lengths[] = {strlen(host), strlen(serv)};
  for (int output = 0; output < 2; output++)
    for (socklen_t size = 0; size <= lengths[output] + 1; size++) {
      char actual[2][80];
      memset(actual, '#', sizeof(actual));
      socklen_t sizes[] = {sizeof(actual[0]), sizeof(actual[1])};
      sizes[output] = size;
      int ret =
          getnameinfo(sa, len, actual[0], sizes[0], actual[1], sizes[1], flags);
      assert(ret == 0 ||
             (ret == EAI_OVERFLOW && size && size <= lengths[output]));
      for (int i = 0; i < 2; i++) {
        if (ret == 0 && sizes[i]) {
          assert(memchr(actual[i], 0, sizes[i]));
          if (i == 0)
            check_host(actual[i], expected_host, sa->sa_family);
          else
            assert(strcmp(actual[i], expected_serv) == 0);
        }
        for (size_t byte = ret ? 0 : sizes[i]; byte < sizeof(actual[i]); byte++)
          assert(actual[i][byte] == '#');
      }
    }
}

int main(void) {
  struct sockaddr_in addr4 = {.sin_family = AF_INET, .sin_port = htons(65535)};
  struct sockaddr_in6 addr6 = {.sin6_family = AF_INET6,
                               .sin6_port = htons(4242)};
  const struct sockaddr *sa = (const void *)&addr4;
  const struct sockaddr *sa6 = (const void *)&addr6;
  const int numeric = NI_NUMERICHOST | NI_NUMERICSERV;
  char host[80], serv[32];
  assert(inet_pton(AF_INET, "192.0.2.1", &addr4.sin_addr) == 1);
  assert(inet_pton(AF_INET6, "::1", &addr6.sin6_addr) == 1);
  check_nameinfo(sa, sizeof(addr4), numeric, "192.0.2.1", "65535");
  check_nameinfo(sa6, sizeof(addr6), numeric, "::1", "4242");
  addr6.sin6_scope_id = UINT32_MAX;
  check_nameinfo(sa6, sizeof(addr6), numeric | NI_NUMERICSCOPE,
                 "::1%4294967295", "4242");
  check_nameinfo(sa6, sizeof(addr6), NI_NOFQDN, "::1%4294967295", "4242");
  addr6.sin6_scope_id = 0;
  assert(inet_pton(AF_INET6, "::ffff:192.0.2.1", &addr6.sin6_addr) == 1);
  check_nameinfo(sa6, sizeof(addr6), numeric, "::ffff:192.0.2.1", "4242");
  assert(inet_pton(AF_INET6, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
                   &addr6.sin6_addr) == 1);
  addr6.sin6_scope_id = UINT32_MAX;
  const char *scoped_host =
      "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff%4294967295";
  check_nameinfo(sa6, sizeof(addr6), numeric, scoped_host, "4242");

  addr4.sin_port = htons(80);
  check_nameinfo(sa, sizeof(addr4), 0, "192.0.2.1", "http");
  check_nameinfo(sa, sizeof(addr4), NI_DGRAM, "192.0.2.1", "http");
  check_nameinfo(sa, sizeof(addr4), numeric, "192.0.2.1", "80");
  addr4.sin_port = htons(21);
  check_nameinfo(sa, sizeof(addr4), 0, "192.0.2.1", "ftp");
  check_nameinfo(sa, sizeof(addr4), NI_DGRAM, "192.0.2.1", "21");
  addr4.sin_port = htons(20);
  check_nameinfo(sa, sizeof(addr4), 0, "192.0.2.1", "ftp-data");
  addr4.sin_port = htons(53);
  check_nameinfo(sa, sizeof(addr4), NI_DGRAM, "192.0.2.1", "domain");
  addr4.sin_port = 0;
  check_nameinfo(sa, sizeof(addr4), 0, "192.0.2.1", "0");

  assert(getnameinfo(sa, sizeof(addr4), host, sizeof(host), NULL, 0,
                     NI_NAMEREQD) == EAI_NONAME);
  assert(getnameinfo(sa, sizeof(addr4), host, sizeof(host), NULL, 0,
                     numeric | NI_NAMEREQD) == EAI_NONAME);
  assert(getnameinfo(sa, sizeof(addr4), host, 0, serv, sizeof(serv),
                     NI_NAMEREQD) == 0);
  assert(strcmp(serv, "0") == 0);
  assert(getnameinfo(sa, sizeof(addr4), NULL, 99, serv, sizeof(serv),
                     numeric) == 0);
  assert(getnameinfo(sa, sizeof(addr4), host, sizeof(host), NULL, 99,
                     numeric) == 0);
  assert(getnameinfo(sa, sizeof(addr4), NULL, 0, NULL, 0, numeric) ==
         EAI_NONAME);
  assert(getnameinfo(sa, sizeof(addr4), host, 0, serv, 0, numeric) ==
         EAI_NONAME);
  assert(getnameinfo(sa, sizeof(addr4), host, sizeof(host), serv, sizeof(serv),
                     0x8000) == EAI_BADFLAGS);
  assert(getnameinfo(NULL, 0, host, sizeof(host), serv, sizeof(serv),
                     numeric) == EAI_FAMILY);
  for (socklen_t len = 0; len < sizeof(addr4); len++)
    assert(getnameinfo(sa, len, host, sizeof(host), serv, sizeof(serv),
                       numeric) == EAI_FAMILY);
  for (socklen_t len = 0; len < sizeof(addr6); len++)
    assert(getnameinfo(sa6, len, host, sizeof(host), serv, sizeof(serv),
                       numeric) == EAI_FAMILY);
  addr4.sin_family = AF_UNSPEC;
  assert(getnameinfo(sa, sizeof(addr4), host, sizeof(host), serv, sizeof(serv),
                     numeric) == EAI_FAMILY);
  addr6.sin6_family = AF_UNSPEC;
  assert(getnameinfo(sa6, sizeof(addr6), host, sizeof(host), serv, sizeof(serv),
                     numeric) == EAI_FAMILY);
  const void *end = (void *)(__builtin_wasm_memory_size(0) * 65536);
  assert(getnameinfo(end, 0, host, sizeof(host), serv, sizeof(serv), numeric) ==
         EAI_FAMILY);
}
