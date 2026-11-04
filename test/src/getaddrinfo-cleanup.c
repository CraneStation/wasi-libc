#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <wasi/wasip2.h>

static int deferred, address_count, start_error, next_address, ready;
static struct addrinfo hints;
static unsigned address_mask;
static int stream_live, next_pollable;
static uint64_t live_pollables;
static int allocations, fail_allocation, track_allocations;
static ip_name_lookup_error_code_t resolver_error;
enum { STREAM = 0x70000000, POLLABLE };

static uint64_t pollable_bit(int handle) {
  assert(handle >= POLLABLE && handle < next_pollable);
  return 1ULL << (handle - POLLABLE);
}

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
  if (track_allocations && ++allocations == fail_allocation)
    return NULL;
  return __real_malloc(size);
}

bool __wrap_ip_name_lookup_resolve_addresses(
    ip_name_lookup_borrow_network_t network, wasip2_string_t *name,
    ip_name_lookup_own_resolve_address_stream_t *ret,
    ip_name_lookup_error_code_t *err) {
  assert(!stream_live && !live_pollables);
  if (start_error) {
    *err = resolver_error;
    return false;
  }
  stream_live = 1;
  ret->__handle = STREAM;
  return true;
}

bool __wrap_ip_name_lookup_method_resolve_address_stream_resolve_next_address(
    ip_name_lookup_borrow_resolve_address_stream_t self,
    ip_name_lookup_option_ip_address_t *ret, ip_name_lookup_error_code_t *err) {
  assert(self.__handle == STREAM && stream_live);
  if (!ready) {
    *err = NETWORK_ERROR_CODE_WOULD_BLOCK;
    return false;
  }
  if (next_address == address_count && resolver_error) {
    *err = resolver_error;
    return false;
  }
  *ret = (ip_name_lookup_option_ip_address_t){0};
  ret->is_some = next_address < address_count;
  ret->val.tag =
      next_address == 0 ? NETWORK_IP_ADDRESS_IPV4 : NETWORK_IP_ADDRESS_IPV6;
  if (next_address == 0) {
    ret->val.val.ipv4.f0 = 127;
    ret->val.val.ipv4.f3 = 1;
  } else {
    ret->val.val.ipv6.f7 = 1;
  }
  next_address++;
  ready = !deferred;
  return true;
}

ip_name_lookup_own_pollable_t
__wrap_ip_name_lookup_method_resolve_address_stream_subscribe(
    ip_name_lookup_borrow_resolve_address_stream_t self) {
  assert(self.__handle == STREAM && stream_live);
  assert(next_pollable - POLLABLE < 64);
  int handle = next_pollable++;
  live_pollables |= pollable_bit(handle);
  return (ip_name_lookup_own_pollable_t){handle};
}

void __wrap_ip_name_lookup_resolve_address_stream_drop_own(
    ip_name_lookup_own_resolve_address_stream_t self) {
  assert(self.__handle == STREAM && stream_live && !live_pollables);
  stream_live = 0;
}

void __real_poll_pollable_drop_own(poll_own_pollable_t self);
void __wrap_poll_pollable_drop_own(poll_own_pollable_t self) {
  if (self.__handle < POLLABLE) {
    __real_poll_pollable_drop_own(self);
    return;
  }
  uint64_t bit = pollable_bit(self.__handle);
  assert(live_pollables & bit);
  live_pollables &= ~bit;
}

void __real_poll_method_pollable_block(poll_borrow_pollable_t self);
void __wrap_poll_method_pollable_block(poll_borrow_pollable_t self) {
  if (self.__handle < POLLABLE) {
    __real_poll_method_pollable_block(self);
    return;
  }
  assert(stream_live && (live_pollables & pollable_bit(self.__handle)));
  ready = 1;
}

static int check_lookup(int expected, int fail_at) {
  struct addrinfo *result = (void *)1;
  next_address = allocations = 0;
  next_pollable = POLLABLE;
  ready = !deferred;
  fail_allocation = fail_at;
  track_allocations = 1;
  int ret = getaddrinfo("lookup.test", "443", &hints, &result);
  track_allocations = 0;
  assert(ret == expected || (fail_at && ret == EAI_MEMORY));
  assert(!stream_live && !live_pollables);
  if (ret == 0) {
    unsigned seen = 0;
    assert(result);
    if (hints.ai_flags & AI_CANONNAME)
      assert(result->ai_canonname && result->ai_canonname[0]);
    for (const struct addrinfo *p = result; p; p = p->ai_next) {
      assert(p->ai_socktype == SOCK_STREAM || p->ai_socktype == SOCK_DGRAM);
      assert(p->ai_protocol ==
             (p->ai_socktype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP));
      bool ipv6 = p->ai_family == AF_INET6;
      const struct sockaddr_in *addr4 = (void *)p->ai_addr;
      const struct sockaddr_in6 *addr6 = (void *)p->ai_addr;
      assert(p->ai_family == AF_INET || ipv6);
      assert(p->ai_addrlen == (ipv6 ? sizeof(*addr6) : sizeof(*addr4)));
      assert(p->ai_addr->sa_family == p->ai_family);
      assert((ipv6 ? addr6->sin6_port : addr4->sin_port) == htons(443));
      assert(ipv6 ? IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr)
                  : addr4->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
      unsigned bit = 2 * ipv6 + (p->ai_socktype == SOCK_DGRAM);
      assert(!(seen & (1u << bit)));
      seen |= 1u << bit;
    }
    assert(seen == (((1u << (2 * address_count)) - 1) & address_mask));
  } else {
    assert(result == NULL);
  }
  freeaddrinfo(result);
  return allocations;
}

int main(void) {
  const struct {
    ip_name_lookup_error_code_t host;
    int expected;
  } errors[] = {{0, 0},
                {NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE, EAI_AGAIN},
                {NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE, EAI_FAIL},
                {NETWORK_ERROR_CODE_NAME_UNRESOLVABLE, EAI_NONAME}};
  const struct {
    struct addrinfo hints;
    unsigned mask;
  } requests[] = {
      {{.ai_flags = AI_CANONNAME}, 0xf},
      {{.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, 0x1},
      {{.ai_socktype = SOCK_DGRAM}, 0xa},
  };
  for (size_t request = 0; request < sizeof(requests) / sizeof(requests[0]);
       request++) {
    hints = requests[request].hints;
    address_mask = requests[request].mask;
    for (int repeat = 0; repeat < 8; repeat++)
      for (deferred = 0; deferred < 2; deferred++)
        for (size_t error = 0; error < sizeof(errors) / sizeof(errors[0]);
             error++) {
          resolver_error = errors[error].host;
          start_error = resolver_error != 0;
          if (start_error)
            check_lookup(errors[error].expected, 0);
          start_error = 0;
          for (address_count = resolver_error ? 0 : 1; address_count <= 2;
               address_count++) {
            int allocation_points = check_lookup(errors[error].expected, 0);
            for (int fail_at = 1; fail_at <= allocation_points; fail_at++)
              check_lookup(errors[error].expected, fail_at);
          }
        }
  }
}
