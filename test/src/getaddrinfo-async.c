#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/wasip3.h>

bool __wasilibc_waitable_block_on(uint32_t waitable, wasip3_event_t *event,
                                  monotonic_clock_duration_t timeout);
void __wasilibc_poll_waitable(uint32_t waitable, wasip3_event_t *event);
void __wasilibc_subtask_block_on_and_drop(wasip3_subtask_t subtask);

enum { LOOKUP_SUCCESS = -1, LOOKUP_PENDING = -2 };

static int lookup_outcome = LOOKUP_SUCCESS, defer_lookup,
           allocations_until_failure, allocation_failure_at, subtask_pending,
           subtasks_dropped;
static void *result_allocation;
static bool empty_error_message;
static const uint8_t *borrowed_name;
static uint8_t *pending_result;
static wasip3_subtask_t pending_subtask;

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
  if (allocations_until_failure && --allocations_until_failure == 0)
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
  subtasks_dropped++;
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
  allocations_until_failure = allocation_failure_at;
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

static int32_t mock_resolve_addresses(uint8_t *name, size_t len,
                                      uint8_t *result) {
  assert(len == 13 && memcmp(name, "async.invalid", len) == 0);
  assert(!result_allocation && !pending_result && !borrowed_name &&
         !pending_subtask);
  borrowed_name = name;
  pending_result = result;
  // Use a timer so tests do not need external DNS.
  wasip3_subtask_status_t status = WASIP3_SUBTASK_RETURNED;
  if (defer_lookup)
    status = monotonic_clock_wait_for(
        lookup_outcome == LOOKUP_PENDING ? 60000000000 : 1000000);
  subtask_pending = WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED;
  if (subtask_pending)
    pending_subtask = WASIP3_SUBTASK_HANDLE(status);
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
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED)
    __wasilibc_subtask_block_on_and_drop(WASIP3_SUBTASK_HANDLE(status));
}

static void check_error(const ip_name_lookup_error_code_t *error) {
  assert(error->tag == lookup_outcome);
  if (error->tag == IP_NAME_LOOKUP_ERROR_CODE_OTHER) {
    assert(error->val.other.is_some == !empty_error_message);
    if (error->val.other.is_some) {
      assert(error->val.other.val.ptr == result_allocation &&
             error->val.other.val.len == 4);
      assert(memcmp(error->val.other.val.ptr, "oops", 4) == 0);
    }
  }
}

static void check_bindings(void) {
  wasip3_string_t name = {(uint8_t *)"async.invalid", 13};
  ip_name_lookup_list_ip_address_t addresses = {(void *)1, 7};
  ip_name_lookup_error_code_t error = {.tag = 255};
  int dropped_before = subtasks_dropped;
  bool ok = ip_name_lookup_resolve_addresses(&name, &addresses, &error);
  assert(ok == (lookup_outcome == LOOKUP_SUCCESS));
  assert(subtasks_dropped == dropped_before + subtask_pending);
  if (ok) {
    assert(addresses.ptr == result_allocation && addresses.len == 2);
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
  dropped_before = subtasks_dropped;
  defer_lookup = !defer_lookup;
  wasip3_subtask_status_t status =
      ip_name_lookup_resolve_addresses_async(name, &result);
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED)
    __wasilibc_subtask_block_on_and_drop(WASIP3_SUBTASK_HANDLE(status));
  defer_lookup = !defer_lookup;
  assert(result.is_err == !ok);
  if (ok)
    assert(result.val.ok.ptr == result_allocation && result.val.ok.len == 2);
  else
    check_error(&result.val.err);
  ip_name_lookup_result_list_ip_address_error_code_free(&result);
  assert(!result_allocation &&
         subtasks_dropped == dropped_before + subtask_pending);
}

static void check_lookup(int expected) {
  struct addrinfo hints = {.ai_socktype = SOCK_STREAM,
                           .ai_flags = AI_CANONNAME};
  struct addrinfo *addresses = NULL;
  int dropped_before = subtasks_dropped;
  assert(getaddrinfo("async.invalid", "443", &hints, &addresses) == expected);
  assert(!result_allocation && !allocations_until_failure &&
         subtasks_dropped == dropped_before + subtask_pending);
  if (expected == 0) {
    assert(addresses && addresses->ai_family == AF_INET);
    assert(addresses->ai_next && addresses->ai_next->ai_family == AF_INET6);
    assert(!addresses->ai_next->ai_next);
    assert(strcmp(addresses->ai_canonname, "async.invalid") == 0);
  } else {
    assert(addresses == NULL);
  }
  freeaddrinfo(addresses);
}

int main(int argc, char **argv) {
  const int errors[] = {EAI_SYSTEM, EAI_SYSTEM, EAI_NONAME,
                        EAI_AGAIN,  EAI_FAIL,   EAI_SYSTEM};
  for (defer_lookup = 0; defer_lookup <= 1; defer_lookup++) {
    lookup_outcome = LOOKUP_SUCCESS;
    check_lookup(0);
    check_bindings();
    for (lookup_outcome = 0; lookup_outcome <= IP_NAME_LOOKUP_ERROR_CODE_OTHER;
         lookup_outcome++) {
      errno = 0;
      check_lookup(errors[lookup_outcome]);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_ACCESS_DENIED)
        assert(errno == EACCES);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_INVALID_ARGUMENT)
        assert(errno == EINVAL);
      if (lookup_outcome == IP_NAME_LOOKUP_ERROR_CODE_OTHER)
        assert(errno == EIO);
      check_bindings();
    }
  }
  defer_lookup = 0;
  lookup_outcome = LOOKUP_SUCCESS;
  for (allocation_failure_at = 1; allocation_failure_at <= 5;
       allocation_failure_at++)
    check_lookup(EAI_MEMORY);
  allocation_failure_at = 0;

  wasip3_string_t name = {(uint8_t *)"async.invalid", 13};
  ip_name_lookup_result_list_ip_address_error_code_t result = {
      .is_err = true, .val.err.tag = 255};
  defer_lookup = 1;
  lookup_outcome = LOOKUP_PENDING;
  for (int i = 0; i < 128; i++) {
    wasip3_subtask_status_t status =
        ip_name_lookup_resolve_addresses_async(name, &result);
    assert(subtask_pending);
    wasip3_subtask_t subtask = WASIP3_SUBTASK_HANDLE(status);
    wasip3_event_t event;
    if (i % 2)
      assert(!__wasilibc_waitable_block_on(subtask, &event, 1000000));
    else {
      __wasilibc_poll_waitable(subtask, &event);
      assert(event.event == WASIP3_EVENT_NONE);
    }
    status = wasip3_subtask_cancel(subtask);
    assert(WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_STARTED_CANCELLED ||
           WASIP3_SUBTASK_STATE(status) == WASIP3_SUBTASK_RETURNED_CANCELLED);
    wasip3_subtask_drop(subtask);
    assert(!pending_result && !borrowed_name && !pending_subtask &&
           !result_allocation);
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
      wasip3_event_t event;
      assert(__wasilibc_waitable_block_on(subtask, &event, 0));
      assert(event.event == WASIP3_EVENT_SUBTASK && event.waitable == subtask &&
             event.code == WASIP3_SUBTASK_RETURNED);
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
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED)
    __wasilibc_subtask_block_on_and_drop(WASIP3_SUBTASK_HANDLE(status));
  if (argc == 2) {
    assert(strcmp(argv[1], "allow") == 0);
    assert(!result.is_err && result.val.ok.len == 1);
    assert(result.val.ok.ptr[0].tag == SOCKETS_IP_ADDRESS_IPV4);
    assert(result.val.ok.ptr[0].val.ipv4.f0 == 127);
    assert(result.val.ok.ptr[0].val.ipv4.f3 == 1);
  } else {
    assert(result.is_err);
  }
  ip_name_lookup_result_list_ip_address_error_code_free(&result);

  ip_name_lookup_list_ip_address_t addresses;
  ip_name_lookup_error_code_t error;
  bool ok = ip_name_lookup_resolve_addresses(&name, &addresses, &error);
  assert(ok == (argc == 2));
  if (ok) {
    assert(addresses.len == 1 &&
           addresses.ptr[0].tag == SOCKETS_IP_ADDRESS_IPV4);
    assert(addresses.ptr[0].val.ipv4.f0 == 127 &&
           addresses.ptr[0].val.ipv4.f3 == 1);
    ip_name_lookup_list_ip_address_free(&addresses);
  } else {
    ip_name_lookup_error_code_free(&error);
  }
}
