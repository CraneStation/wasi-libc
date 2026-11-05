#ifndef __WASI_WASIP3_H
#define __WASI_WASIP3_H

#include <wasi/version.h>

#ifdef __wasip3__
#include <wasi/__generated_wasip3.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns a subtask status. Keep name bytes and result storage alive until
// completion or confirmed cancellation. Pending calls can use a waitable set
// to poll or wait alongside a timer.
// If WASIP3_SUBTASK_STATE(status) is WASIP3_SUBTASK_RETURNED, including after
// wasip3_subtask_cancel, the caller owns the result. Free it with
// ip_name_lookup_result_list_ip_address_error_code_free after use.
// After confirmed cancellation, do not read or free the result.
// Drop pending handles after completion or confirmed cancellation; immediately
// completed calls have no handle. Do not cancel after a terminal event.
// Cancellation does not guarantee that the host stops a running OS lookup.
wasip3_subtask_status_t ip_name_lookup_resolve_addresses_async(
    wasip3_string_t name,
    ip_name_lookup_result_list_ip_address_error_code_t *result);

#ifdef __cplusplus
}
#endif
#endif

#endif
