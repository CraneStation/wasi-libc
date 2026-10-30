#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

static void check_nameinfo(const struct sockaddr *sa, socklen_t len, int flags,
                           const char *expected_host,
                           const char *expected_serv) {
  char host[80], serv[32];
  assert(getnameinfo(sa, len, host, sizeof(host), serv, sizeof(serv), flags) ==
         0);
  assert(strcmp(host, expected_host) == 0);
  assert(strcmp(serv, expected_serv) == 0);
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

  for (socklen_t len = 0; len <= strlen(scoped_host) + 1; len++) {
    memset(host, '#', sizeof(host));
    memset(serv, '#', sizeof(serv));
    int ret = getnameinfo(sa6, sizeof(addr6), host, len, serv, 6, numeric);
    assert(ret == (len > 0 && len <= strlen(scoped_host) ? EAI_OVERFLOW : 0));
    assert(host[len] == '#');
    if (ret != 0 || len == 0)
      assert(host[0] == '#');
    if (ret != 0)
      assert(serv[0] == '#');
    else {
      assert(strcmp(serv, "4242") == 0);
      if (len != 0)
        assert(strcmp(host, scoped_host) == 0);
    }
  }
  for (socklen_t len = 0; len <= 6; len++) {
    memset(host, '#', sizeof(host));
    memset(serv, '#', sizeof(serv));
    int ret =
        getnameinfo(sa, sizeof(addr4), host, sizeof(host), serv, len, numeric);
    assert(ret == (len > 0 && len < 6 ? EAI_OVERFLOW : 0));
    assert(serv[len] == '#');
    if (ret != 0 || len == 0)
      assert(serv[0] == '#');
    if (ret != 0)
      assert(host[0] == '#');
    else {
      assert(strcmp(host, "192.0.2.1") == 0);
      if (len != 0)
        assert(strcmp(serv, "65535") == 0);
    }
  }

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
