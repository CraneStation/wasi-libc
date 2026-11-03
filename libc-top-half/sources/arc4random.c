#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <wasi/api.h>
#include <wasi/libc.h>

void arc4random_buf(void* buffer, size_t len)
{
    // Always call `__wasi_random_get` rather than doing a guest PRNG, because
    // Wasm engines may snapshot or even clone Wasm state, which we don't have
    // any visibility into.
    //
    // We therefore effectively expect that `__wasi_random_get` is "fast",
    // presumably using a PRNG-style implementation rather than a slower
    // raw-entropy-style implementation.
#if defined(__wasip1__)
    int r = __wasi_random_get(buffer, len);
#elif defined(__wasip2__) || defined(__wasip3__) || defined(__webp2__)
    int r = __wasilibc_random(buffer, len);
#else
# error "Unknown WASI version"
#endif

    // `__wasi_random_get` should always succeed.
    if (r != 0) {
        __builtin_trap();
    }
}

uint32_t arc4random(void)
{
    uint32_t v;

    arc4random_buf(&v, sizeof v);

    return v;
}

uint32_t arc4random_uniform(const uint32_t upper_bound)
{
    uint32_t min;
    uint32_t r;

    if (upper_bound < 2U) {
        return 0;
    }
    min = (1U + ~upper_bound) % upper_bound; // = 2**32 mod upper_bound
    do {
        r = arc4random();
    } while (r < min);

    // r is now clamped to a set whose size mod upper_bound == 0.
    // The worst case (2**31+1) requires 2 attempts on average.

    return r % upper_bound;
}
