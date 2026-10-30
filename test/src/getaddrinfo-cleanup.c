#include <assert.h>
#include <netdb.h>
#include <stdlib.h>
#include <wasi/wasip2.h>

enum {
  DEFERRED_SUCCESS,
  DEFERRED_RESOLVER_FAILURE,
  DEFERRED_ALLOCATION_FAILURE,
  IMMEDIATE_SUCCESS,
  IMMEDIATE_RESOLVER_FAILURE,
  IMMEDIATE_ALLOCATION_FAILURE,
  PARTIAL_RESOLVER_FAILURE,
  PARTIAL_ALLOCATION_FAILURE,
  START_RESOLVER_FAILURE,
  SCENARIO_COUNT
};
static int scenario, address_calls, stream_live, pollable_live, fail_malloc;
enum { STREAM = 0x70000000, POLLABLE };

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
  if (fail_malloc) {
    fail_malloc = 0;
    return NULL;
  }
  return __real_malloc(size);
}

bool __wrap_ip_name_lookup_resolve_addresses(
    ip_name_lookup_borrow_network_t network, wasip2_string_t *name,
    ip_name_lookup_own_resolve_address_stream_t *ret,
    ip_name_lookup_error_code_t *err) {
  assert(!stream_live && !pollable_live);
  if (scenario == START_RESOLVER_FAILURE) {
    *err = NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE;
    return false;
  }
  stream_live = 1;
  address_calls = 0;
  ret->__handle = STREAM;
  return true;
}

bool __wrap_ip_name_lookup_method_resolve_address_stream_resolve_next_address(
    ip_name_lookup_borrow_resolve_address_stream_t self,
    ip_name_lookup_option_ip_address_t *ret, ip_name_lookup_error_code_t *err) {
  assert(self.__handle == STREAM && stream_live);
  if (address_calls++ == 0 && scenario <= DEFERRED_ALLOCATION_FAILURE) {
    *err = NETWORK_ERROR_CODE_WOULD_BLOCK;
    return false;
  }
  if (scenario == DEFERRED_RESOLVER_FAILURE ||
      scenario == IMMEDIATE_RESOLVER_FAILURE ||
      (scenario == PARTIAL_RESOLVER_FAILURE && address_calls == 2)) {
    *err = NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE;
    return false;
  }
  *ret = (ip_name_lookup_option_ip_address_t){0};
  ret->is_some =
      address_calls == (scenario <= DEFERRED_ALLOCATION_FAILURE ? 2 : 1) ||
      (scenario == PARTIAL_ALLOCATION_FAILURE && address_calls == 2);
  ret->val.tag = NETWORK_IP_ADDRESS_IPV4;
  ret->val.val.ipv4.f0 = 127;
  ret->val.val.ipv4.f3 = 1;
  if (scenario == DEFERRED_ALLOCATION_FAILURE ||
      scenario == IMMEDIATE_ALLOCATION_FAILURE ||
      (scenario == PARTIAL_ALLOCATION_FAILURE && address_calls == 2))
    fail_malloc = 1;
  return true;
}

ip_name_lookup_own_pollable_t
__wrap_ip_name_lookup_method_resolve_address_stream_subscribe(
    ip_name_lookup_borrow_resolve_address_stream_t self) {
  assert(self.__handle == STREAM && stream_live && !pollable_live);
  pollable_live = 1;
  return (ip_name_lookup_own_pollable_t){POLLABLE};
}

void __wrap_ip_name_lookup_resolve_address_stream_drop_own(
    ip_name_lookup_own_resolve_address_stream_t self) {
  assert(self.__handle == STREAM && stream_live && !pollable_live);
  stream_live = 0;
}

void __real_poll_pollable_drop_own(poll_own_pollable_t self);
void __wrap_poll_pollable_drop_own(poll_own_pollable_t self) {
  if (self.__handle != POLLABLE) {
    __real_poll_pollable_drop_own(self);
    return;
  }
  assert(stream_live && pollable_live);
  pollable_live = 0;
}

void __real_poll_method_pollable_block(poll_borrow_pollable_t self);
void __wrap_poll_method_pollable_block(poll_borrow_pollable_t self) {
  if (self.__handle != POLLABLE) {
    __real_poll_method_pollable_block(self);
    return;
  }
  assert(stream_live && pollable_live);
}

int main(void) {
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
  const int expected[] = {0,         EAI_AGAIN,  EAI_MEMORY,
                          0,         EAI_AGAIN,  EAI_MEMORY,
                          EAI_AGAIN, EAI_MEMORY, EAI_AGAIN};
  for (int i = 0; i < 288; i++) {
    struct addrinfo *addresses = NULL;
    scenario = i % SCENARIO_COUNT;
    int ret = getaddrinfo("cleanup.invalid", "443", &hints, &addresses);
    assert(ret == expected[scenario]);
    assert(!stream_live && !pollable_live && !fail_malloc);
    if (ret == 0)
      assert(addresses != NULL && addresses->ai_family == AF_INET);
    else
      assert(addresses == NULL);
    freeaddrinfo(addresses);
  }
}
