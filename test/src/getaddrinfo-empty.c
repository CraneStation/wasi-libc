#include <assert.h>
#include <netdb.h>
#include <wasi/wasip3.h>

static int with_address, list_live;
static ip_name_lookup_ip_address_t address = {
    .tag = SOCKETS_IP_ADDRESS_IPV4, .val = {.ipv4 = {.f0 = 127, .f3 = 1}}};

bool __wrap_ip_name_lookup_resolve_addresses(
    wasip3_string_t *name, ip_name_lookup_list_ip_address_t *ret,
    ip_name_lookup_error_code_t *err) {
  assert(!list_live);
  *ret = (ip_name_lookup_list_ip_address_t){
      .ptr = with_address ? &address : NULL, .len = with_address};
  list_live = with_address;
  return true;
}

void __wrap_ip_name_lookup_list_ip_address_free(
    ip_name_lookup_list_ip_address_t *list) {
  assert(list->ptr == (list_live ? &address : NULL));
  list_live = 0;
}

int main(void) {
  struct addrinfo hints = {.ai_family = AF_INET6, .ai_socktype = SOCK_STREAM};
  for (with_address = 0; with_address < 2; with_address++) {
    struct addrinfo *result = (void *)1;
    assert(getaddrinfo("lookup.test", "443", &hints, &result) == EAI_NONAME);
    assert(result == NULL && !list_live);
    freeaddrinfo(result);
  }
}
