#include <stdlib.h>
#include <wasi/report-error.h>

_Noreturn void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
	__wasilibc_report_errorf("Assertion failed: %s (%s: %s: %d)", expr, file, func, line);
	abort();
}
