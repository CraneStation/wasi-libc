#include <wasi/wasip3.h>

__attribute__((
    __import_module__("wasi:sockets/ip-name-lookup@0.3.2"),
    __import_name__("[async-lower]resolve-addresses"))) extern int32_t
__wasm_import_ip_name_lookup_resolve_addresses_async(uint8_t *, size_t,
                                                     uint8_t *);

wasip3_subtask_status_t ip_name_lookup_resolve_addresses_async(
    wasip3_string_t name,
    ip_name_lookup_result_list_ip_address_error_code_t *result) {
  return __wasm_import_ip_name_lookup_resolve_addresses_async(
      name.ptr, name.len, (uint8_t *)result);
}
