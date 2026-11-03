/**
 * <wasi/api.h>. This file contains declarations describing the WASI ABI
 * as of "snapshot preview1". It was originally auto-generated from
 * wasi_snapshot_preview1.witx, however WASI is in the process of
 * transitioning to a new IDL and header file generator, and this file
 * is temporarily being manually maintained.
 *
 * @file
 * This file describes the [WASI] interface, consisting of functions, types,
 * and defined values (macros).
 *
 * The interface described here is greatly inspired by [CloudABI]'s clean,
 * thoughtfully-designed, capability-oriented, POSIX-style API.
 *
 * [CloudABI]: https://github.com/NuxiNL/cloudlibc
 * [WASI]: https://github.com/WebAssembly/WASI/
 */

#ifndef __wasi_api_h
#define __wasi_api_h

#include <wasi/version.h>

#if defined(__wasip1__)
#include <wasi/wasip1.h>
#elif defined(__wasip2__)
#include <wasi/wasip2.h>
#elif defined(__wasip3__)
#include <wasi/wasip3.h>
#elif defined(__webp2__)
#include <wasi/webp2.h>
#else
#error "Unsupported WASI version"
#endif

#endif // __wasi_api_h
