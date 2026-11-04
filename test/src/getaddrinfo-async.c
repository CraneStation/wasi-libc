#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/wasip3.h>

enum { LOOKUP_SUCCESS = -1, LOOKUP_PENDING = -2 };

static int lookup_outcome = LOOKUP_SUCCESS, defer_lookup, allocations,
           fail_allocation;
static void *result_allocation;
static bool empty_error_message;
static const uint8_t *borrowed_name;
static uint8_t *pending_result;
static wasip3_subtask_t pending_subtask, lookup_subtask;

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
  if (++allocations == fail_allocation)
    return NULL;
  return __real_malloc(size);
}

void __real_free(void *ptr);
void __wrap_free(void *ptr) {
  if (ptr == result_allocation)
    result_allocation = NULL;
  __real_free(ptr);
}

void __real_wasip3_subtask_drop(wasip3_subtask_t subtask);
void __wrap_wasip3_subtask_drop(wasip3_subtask_t subtask) {
  assert(subtask != pending_subtask);
  if (subtask == lookup_subtask)
    lookup_subtask = 0;
  __real_wasip3_subtask_drop(subtask);
}

int32_t __real___wasm_import_ip_name_lookup_resolve_addresses_async(
    uint8_t *name, size_t len, uint8_t *result);
static void complete_lookup(void) {
  assert(pending_result && memcmp(borrowed_name, "async.invalid", 13) == 0);
  ip_name_lookup_result_list_ip_address_error_code_t result = {0};
  ip_name_lookup_result_list_ip_address_error_code_t *lookup_result = &result;
  if (lookup_outcome == LOOKUP_SUCCESS) {
    result_allocation = __real_malloc(2 * sizeof(ip_name_lookup_ip_address_t));
    assert(result_allocation);
    lookup_result->val.ok =
        (ip_name_lookup_list_ip_address_t){result_allocation, 2};
    lookup_result->val.ok.ptr[0] = (ip_name_lookup_ip_address_t){
        .tag = SOCKETS_IP_ADDRESS_IPV4, .val.ipv4 = {127, 0, 0, 1}};
    lookup_result->val.ok.ptr[1] = (ip_name_lookup_ip_address_t){
        .tag = SOCKETS_IP_ADDRESS_IPV6, .val.ipv6.f7 = 1};
  } else if (lookup_outcome >= 0) {
    lookup_result->is_err = true;
    lookup_result->val.err.tag = lookup_outcome;
    if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_OTHER &&
        !empty_error_message) {
      result_allocation = __real_malloc(4);
      assert(result_allocation);
      memcpy(result_allocation, "oops", 4);
      lookup_result->val.err.val.other.is_some = true;
      lookup_result->val.err.val.other.val =
          (wasip3_string_t){result_allocation, 4};
    }
    // An absent option can contain an invalid payload.
    if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_OTHER &&
        empty_error_message)
      lookup_result->val.err.val.other.val = (wasip3_string_t){(uint8_t *)1, 4};
  }
  memcpy(pending_result, lookup_result, sizeof(*lookup_result));
  pending_result = NULL;
  borrowed_name = NULL;
  pending_subtask = 0;
}

static void complete_event(const wasip3_event_t *event) {
  if (pending_result && event->event == WASIP3_EVENT_SUBTASK &&
      event->waitable == pending_subtask &&
      event->code == WASIP3_SUBTASK_RETURNED)
    complete_lookup();
}

void __real_wasip3_waitable_set_wait(wasip3_waitable_set_t set,
                                     wasip3_event_t *event);
void __wrap_wasip3_waitable_set_wait(wasip3_waitable_set_t set,
                                     wasip3_event_t *event) {
  __real_wasip3_waitable_set_wait(set, event);
  complete_event(event);
}

void __real_wasip3_waitable_set_poll(wasip3_waitable_set_t set,
                                     wasip3_event_t *event);
void __wrap_wasip3_waitable_set_poll(wasip3_waitable_set_t set,
                                     wasip3_event_t *event) {
  __real_wasip3_waitable_set_poll(set, event);
  complete_event(event);
}

wasip3_subtask_status_t __real_wasip3_subtask_cancel(wasip3_subtask_t subtask);
wasip3_subtask_status_t __wrap_wasip3_subtask_cancel(wasip3_subtask_t subtask) {
  wasip3_subtask_status_t status = __real_wasip3_subtask_cancel(subtask);
  if (subtask == pending_subtask) {
    if (WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_RETURNED) {
      complete_lookup();
    } else {
      assert(WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_STARTED_CANCELLED ||
             WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_RETURNED_CANCELLED);
      pending_result = NULL;
      borrowed_name = NULL;
      pending_subtask = 0;
    }
  }
  return status;
}

static bool wait_for_task(wasip3_subtask_t task, bool poll) {
  wasip3_waitable_set_t set = wasip3_waitable_set_new();
  wasip3_waitable_join(task, set);
  wasip3_event_t event;
  if (poll)
    wasip3_waitable_set_poll(set, &event);
  else
    wasip3_waitable_set_wait(set, &event);
  wasip3_waitable_join(task, 0);
  wasip3_waitable_set_drop(set);
  assert(event.event == WASIP3_EVENT_NONE ||
         (event.event == WASIP3_EVENT_SUBTASK && event.waitable == task &&
          event.code == WASIP3_SUBTASK_RETURNED));
  return event.event != WASIP3_EVENT_NONE;
}

static void finish_task(wasip3_subtask_status_t status) {
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED) {
    wasip3_subtask_t task = WASIP3_SUBTASK_HANDLE(status);
    assert(wait_for_task(task, false));
    wasip3_subtask_drop(task);
  }
}

static int32_t mock_resolve_addresses(uint8_t *name, size_t len,
                                      uint8_t *result) {
  assert(len == 13 && memcmp(name, "async.invalid", len) == 0);
  assert(!result_allocation && !pending_result && !borrowed_name &&
         !pending_subtask && !lookup_subtask);
  borrowed_name = name;
  pending_result = result;
  // Use a timer so tests do not need external DNS.
  wasip3_subtask_status_t status = WASIP3_SUBTASK_RETURNED;
  if (defer_lookup)
    status = monotonic_clock_wait_for(
        lookup_outcome == LOOKUP_PENDING ? 60000000000 : 1000000);
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED)
    lookup_subtask = pending_subtask = WASIP3_SUBTASK_HANDLE(status);
  else
    complete_lookup();
  return status;
}

int32_t __wrap___wasm_import_ip_name_lookup_resolve_addresses_async(
    uint8_t *name, size_t len, uint8_t *result) {
  if (len == 9 && memcmp(name, "127.0.0.1", len) == 0)
    return __real___wasm_import_ip_name_lookup_resolve_addresses_async(
        name, len, result);
  return mock_resolve_addresses(name, len, result);
}

void __real___wasm_import_ip_name_lookup_resolve_addresses(uint8_t *name,
                                                           size_t len,
                                                           uint8_t *result);
void __wrap___wasm_import_ip_name_lookup_resolve_addresses(uint8_t *name,
                                                           size_t len,
                                                           uint8_t *result) {
  if (len == 9 && memcmp(name, "127.0.0.1", len) == 0) {
    __real___wasm_import_ip_name_lookup_resolve_addresses(name, len, result);
    return;
  }
  wasip3_subtask_status_t status = mock_resolve_addresses(name, len, result);
  finish_task(status);
}

static void check_error(const ip_name_lookup_error_code_t *error) {
  assert(error->tag == lookup_outcome);
  if (error->tag == IP_NAME_LOOKUP_ERROR_CODE_OTHER) {
    assert(error->val.other.is_some == !empty_error_message);
    if (error->val.other.is_some) {
      assert(error->val.other.val.len == 4);
      assert(memcmp(error->val.other.val.ptr, "oops", 4) == 0);
    }
  }
}

static void check_addresses(const ip_name_lookup_list_ip_address_t *addresses,
                            size_t count) {
  assert(addresses->ptr && addresses->len == count);
  const sockets_ipv4_address_t *addr = &addresses->ptr[0].val.ipv4;
  assert(addresses->ptr[0].tag == SOCKETS_IP_ADDRESS_IPV4 && addr->f0 == 127 &&
         addr->f1 == 0 && addr->f2 == 0 && addr->f3 == 1);
  if (count == 2) {
    const sockets_ipv6_address_t *addr6 = &addresses->ptr[1].val.ipv6;
    assert(addresses->ptr[1].tag == SOCKETS_IP_ADDRESS_IPV6 && !addr6->f0 &&
           !addr6->f1 && !addr6->f2 && !addr6->f3 && !addr6->f4 && !addr6->f5 &&
           !addr6->f6 && addr6->f7 == 1);
  }
}

static void check_bindings(void) {
  wasip3_string_t name = {(uint8_t *)"async.invalid", 13};
  ip_name_lookup_list_ip_address_t addresses = {(void *)1, 7};
  ip_name_lookup_error_code_t error = {.tag = 255};
  bool ok = ip_name_lookup_resolve_addresses(&name, &addresses, &error);
  assert(ok == (lookup_outcome == LOOKUP_SUCCESS) && !lookup_subtask);
  if (ok) {
    check_addresses(&addresses, 2);
    assert(error.tag == 255);
    ip_name_lookup_list_ip_address_free(&addresses);
  } else {
    assert(addresses.ptr == (void *)1 && addresses.len == 7);
    check_error(&error);
    ip_name_lookup_error_code_free(&error);
  }
  assert(!result_allocation);

  ip_name_lookup_result_list_ip_address_error_code_t result = {
      .is_err = true, .val.err.tag = 255};
  defer_lookup = !defer_lookup;
  wasip3_subtask_status_t status =
      ip_name_lookup_resolve_addresses_async(name, &result);
  finish_task(status);
  defer_lookup = !defer_lookup;
  assert(result.is_err == !ok);
  if (ok)
    check_addresses(&result.val.ok, 2);
  else
    check_error(&result.val.err);
  ip_name_lookup_result_list_ip_address_error_code_free(&result);
  assert(!result_allocation && !lookup_subtask);
}

static int check_lookup(int expected, int fail_at) {
  struct addrinfo hints = {.ai_socktype = SOCK_STREAM,
                           .ai_flags = AI_CANONNAME};
  struct addrinfo *addresses = (void *)1;
  allocations = 0;
  fail_allocation = fail_at;
  int ret = getaddrinfo("async.invalid", "443", &hints, &addresses);
  fail_allocation = 0;
  assert(ret == expected || (fail_at && ret == EAI_MEMORY));
  assert(!result_allocation && !lookup_subtask);
  if (ret == 0) {
    unsigned seen = 0;
    assert(addresses && addresses->ai_canonname && addresses->ai_canonname[0]);
    for (struct addrinfo *p = addresses; p; p = p->ai_next) {
      bool ipv6 = p->ai_family == AF_INET6;
      const struct sockaddr_in *addr4 = (void *)p->ai_addr;
      const struct sockaddr_in6 *addr6 = (void *)p->ai_addr;
      assert(p->ai_family == AF_INET || ipv6);
      assert(p->ai_socktype == SOCK_STREAM && p->ai_protocol == IPPROTO_TCP);
      assert(p->ai_addrlen == (ipv6 ? sizeof(*addr6) : sizeof(*addr4)));
      assert(p->ai_addr->sa_family == p->ai_family);
      assert((ipv6 ? addr6->sin6_port : addr4->sin_port) == htons(443));
      assert(ipv6 ? IN6_IS_ADDR_LOOPBACK(&addr6->sin6_addr)
                  : addr4->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
      assert(!(seen & (1u << ipv6)));
      seen |= 1u << ipv6;
    }
    assert(seen == 3);
  } else {
    assert(addresses == NULL);
  }
  freeaddrinfo(addresses);
  return allocations;
}

int main(int argc, char **argv) {
  const int errors[] = {EAI_SYSTEM, EAI_SYSTEM, EAI_NONAME,
                        EAI_AGAIN,  EAI_FAIL,   EAI_SYSTEM};
  for (defer_lookup = 0; defer_lookup <= 1; defer_lookup++) {
    lookup_outcome = LOOKUP_SUCCESS;
    int allocation_points = check_lookup(0, 0);
    for (int fail_at = 1; fail_at <= allocation_points; fail_at++)
      check_lookup(0, fail_at);
    check_bindings();
    for (lookup_outcome = 0; lookup_outcome <= IP_NAME_LOOKUP_ERROR_CODE_OTHER;
         lookup_outcome++) {
      errno = 0;
      check_lookup(errors[lookup_outcome], 0);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_ACCESS_DENIED)
        assert(errno == EACCES);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_INVALID_ARGUMENT)
        assert(errno == EINVAL);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_OTHER)
        assert(errno == EIO);
      check_bindings();
    }
  }
  wasip3_string_t name = {(uint8_t *)"async.invalid", 13};
  ip_name_lookup_result_list_ip_address_error_code_t result = {
      .is_err = true, .val.err.tag = 255};
  defer_lookup = 1;
  lookup_outcome = LOOKUP_PENDING;
  for (int i = 0; i < 128; i++) {
    wasip3_subtask_status_t status =
        ip_name_lookup_resolve_addresses_async(name, &result);
    assert(WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED);
    wasip3_subtask_t subtask = WASIP3_SUBTASK_HANDLE(status);
    if (i % 2)
      finish_task(monotonic_clock_wait_for(1000000));
    assert(!wait_for_task(subtask, true));
    status = wasip3_subtask_cancel(subtask);
    assert(WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_STARTED_CANCELLED ||
           WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_RETURNED_CANCELLED);
    wasip3_subtask_drop(subtask);
    assert(!pending_result && !borrowed_name && !pending_subtask &&
           !result_allocation && !lookup_subtask);
  }

  for (int i = 0; i < 3; i++) {
    lookup_outcome = i ? IP_NAME_LOOKUP_ERROR_CODE_OTHER : LOOKUP_SUCCESS;
    empty_error_message = i == 2;
    wasip3_subtask_t subtask = 0;
    result = (ip_name_lookup_result_list_ip_address_error_code_t){
        .is_err = true, .val.err.tag = 255};
    wasip3_subtask_status_t status =
        ip_name_lookup_resolve_addresses_async(name, &result);
    if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED) {
      subtask = WASIP3_SUBTASK_HANDLE(status);
      assert(wait_for_task(subtask, false));
    }
    assert(result.is_err == (lookup_outcome >= 0));
    ip_name_lookup_result_list_ip_address_error_code_free(&result);
    assert(!result_allocation);
    if (subtask != 0)
      wasip3_subtask_drop(subtask);
    check_bindings();
  }

  name = (wasip3_string_t){(uint8_t *)"127.0.0.1", 9};
  wasip3_subtask_status_t status =
      ip_name_lookup_resolve_addresses_async(name, &result);
  finish_task(status);
  if (argc == 2) {
    assert(strcmp(argv[1], "allow") == 0);
    assert(!result.is_err);
    check_addresses(&result.val.ok, 1);
  } else {
    assert(result.is_err);
  }
  ip_name_lookup_result_list_ip_address_error_code_free(&result);

  ip_name_lookup_list_ip_address_t addresses = {(void *)1, 7};
  ip_name_lookup_error_code_t error = {.tag = 255};
  bool ok = ip_name_lookup_resolve_addresses(&name, &addresses, &error);
  assert(ok == (argc == 2));
  if (ok) {
    check_addresses(&addresses, 1);
    assert(error.tag == 255);
    ip_name_lookup_list_ip_address_free(&addresses);
  } else {
    assert(addresses.ptr == (void *)1 && addresses.len == 7);
    ip_name_lookup_error_code_free(&error);
  }
}
