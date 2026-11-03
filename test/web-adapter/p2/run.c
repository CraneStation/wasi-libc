// Linked into all webp2 test programs to give them a `wasi:cli/run` export.
// See the `test-command` world in `adapter.wit`.

#include "test_command.h"

bool exports_wasi_cli_run_run(void) { return true; }
