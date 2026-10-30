#ifndef __WASI_WASIP3_H
#define __WASI_WASIP3_H

#include <wasi/version.h>

#ifdef __wasip3__
#include <wasi/__generated_wasip3.h>

#ifdef __cplusplus
extern "C" {
#endif

// Keep input bytes and result alive until completion or confirmed cancellation.
wasip3_subtask_status_t ip_name_lookup_resolve_addresses_async(
    wasip3_string_t name,
    ip_name_lookup_result_list_ip_address_error_code_t *result);

#ifdef __cplusplus
}
#endif
#endif

#endif
