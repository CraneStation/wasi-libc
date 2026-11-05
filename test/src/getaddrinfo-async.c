#include <wasi/wasip3.h>

#include "test.h"

int main(void) {
  wasip3_string_t name;
  wasip3_string_set(&name, "localhost");
  ip_name_lookup_result_list_ip_address_error_code_t result = {.is_err = true};
  wasip3_subtask_status_t status =
      ip_name_lookup_resolve_addresses_async(name, &result);

  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED) {
    wasip3_subtask_t task = WASIP3_SUBTASK_HANDLE(status);
    wasip3_waitable_set_t set = wasip3_waitable_set_new();
    wasip3_waitable_join(task, set);
    wasip3_event_t event;
    do {
      wasip3_waitable_set_wait(set, &event);
    } while (event.code != WASIP3_SUBTASK_RETURNED);
    wasip3_waitable_join(task, 0);
    wasip3_subtask_drop(task);
    wasip3_waitable_set_drop(set);
  }

  if (result.is_err || result.val.ok.len == 0)
    t_error("expected an address for localhost\n");
  ip_name_lookup_result_list_ip_address_error_code_free(&result);
  result.is_err =
      !ip_name_lookup_resolve_addresses(&name, &result.val.ok, &result.val.err);
  if (result.is_err || result.val.ok.len == 0)
    t_error("expected an address from the blocking lookup\n");
  ip_name_lookup_result_list_ip_address_error_code_free(&result);
  return t_status;
}
