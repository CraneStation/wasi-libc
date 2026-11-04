#include <wchar.h>

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
	const wchar_t *p;
	p = s+wcslen(s);
	while (*p != c) {
		if (p == s) return 0;
		p--;
	}
	return (wchar_t *)p;
}
