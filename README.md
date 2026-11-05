# `wasi-libc`

`wasi-libc` is a libc for WebAssembly programs built on top of WASI system
calls. It provides a wide array of POSIX-compatible C APIs, including support
for standard I/O, file I/O, filesystem manipulation, memory management, time,
string, environment variables, program startup, and many other APIs.

`wasi-libc` is sufficiently stable and usable for many purposes, as most of the
POSIX-compatible APIs are stable, though it is continuing to evolve to better
align with wasm and WASI. For example, pthread support is experimentally
provided via the [wasi-threads] proposal.

[wasi-threads]: https://github.com/WebAssembly/wasi-threads

## Usage

The easiest way to get started with this is to use [wasi-sdk], which includes a
build of `wasi-libc` in its sysroot.

## Building from source

To build a WASI sysroot from source, obtain a WebAssembly-supporting C compiler
(currently this is only clang 10+, though we'd like to support other compilers
as well). The build is configured with:

```sh
cmake -S . -B build
```

You can configure the build system with the `-G` argument to `cmake`, for
example `-G Ninja`, to build with a different build system instead.

Supported options to the configuration step are:

* `-DCMAKE_C_COMPILER=clang` - specify a C compiler to use.
* `-DCMAKE_AR=llvm-ar` - specify a specific `ar` program to use.
* `-DCMAKE_INSTALL_PREFIX=path` - where to install the sysroot.
* `-DTARGET_TRIPLE=...` - which target to build: for example `wasm32-wasip1`,
  `wasm32-wasip2`, `wasm32-wasip1-threads`, ...
* `-DMALLOC=...` - one of `dlmalloc`, `emmalloc`, or `none`.
* `-DBUILTINS_LIB=...` - optional path to compiler-rt which, if not present,
  will be downloaded automatically.
* `-DBULK_MEMORY_THRESHOLD=...` - used to tune bulk-memory-related copying
  functions.
* `-DSETJMP=(ON|OFF)` - whether or not to build libsetjmp.{a,so}
* `-DBUILD_TESTS=(ON|OFF)` - whether or not to build tests
* `-DSIMD=(ON|OFF)` - whether or not to enable simd-related string routines
* `-DBUILD_SHARED=(ON|OFF)` - forcibly enable or disable building shared
  libraries.

After the build is configured you can use the generated build system to build
wasi-libc. For example.

```sh
cd build && make -j$(nproc)
```

This can then be installed using the `install` target of your build system. For
example:

```sh
cd build && make install
```

This will install to the directory specified by `-DCMAKE_INSTALL_PREFIX=...`.
This path can then be used as a sysroot for C:

```sh
/path/to/wasm/supporting/c/compiler --sysroot=/path/to/the/newly/built/sysroot ...
```

to run the compiler using the newly built sysroot.

## Other default libraries

Note that Clang packages typically don't include cross-compiled builds of
compiler-rt, libcxx, or libcxxabi, for `libclang_rt.builtins-wasm32.a`,
`libc++.a`, or `libc++abi.a`, respectively. This means that by default just
passing `--sysroot` may not be enough as Clang will also look for some of these
libraries, notably `libclang_rt.builtins-wasm32.a`, by default. This is one of
the things [wasi-sdk] simplifies, as it includes cross-compiled builds of
compiler-rt, `libc++.a`, and `libc++abi.a`.

The build of wasi-libc itself depends on compiler-rt, or
`libclang_rt.builtins-wasm32.a`. This manifests in error messages such as
`undefined symbol: __muloti4` for example. Specifically the PIC build of
wasi-libc will require that Clang can locate `libclang_rt.builtins-wasm32.a`,
meaning that `libc.so` requires this library to be present. This library is
downloaded automatically as part of the build but it can also be manually
specified with `-DBUILTINS_LIB=...`.

## Building in pthread support

All targets in wasi-libc have `pthread_*`-related symbols defined. The behavior
of these symbols differ based on target, however:

* `wasm32-wasip{1,2}` - symbols return an error or trap as appropriate. For
  example spawning a thread returns an error and blocking on a condition
  variable traps (as it can't possibly be woken).

* `wasm32-wasip1-threads` - spawning a thread is done with the (now largely
  deprecated) `wasi-threads` proposal and blocking is done with WebAssembly
  `memory.atomic.*` instructions.

* `wasm32-wasip3` - at this time this behaves the same as `wasm32-wasip{1,2}` by
  default. When wasi-libc is built with `-DENABLE_COOP_THREADS=ON` then
  wasi-libc will use the cooperative multithreading intrinsics of the component
  model (the 🧵 gate in the [component model explainer][explainer]). This means
  that in this mode all `pthread_*` symbols are implemented and work as expected
  and scheduling is implemented with cooperative switches (explicit yields or
  synchronization points in pthread primitives). For more information see [this
  documentation in wasi-sdk][pthread-sdk-docs] for how to use a pre-built copy
  of wasi-libc and avoid needing to build your own sysroot.

[explainer]: https://github.com/WebAssembly/component-model/blob/main/design/mvp/Explainer.md
[pthread-sdk-docs]: https://github.com/WebAssembly/wasi-sdk/blob/main/CoopThreading.md

## Arch Linux AUR package

For Arch Linux users, there's an official [wasi-libc] package tracking this Git
repository. You might want to install other [WASI related packages] as well.

[wasi-sdk]: https://github.com/WebAssembly/wasi-sdk
[wasi-libc]: https://archlinux.org/packages/extra/any/wasi-libc/
[WASI related packages]: https://archlinux.org/packages/?q=wasi-
