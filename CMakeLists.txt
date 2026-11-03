cmake_minimum_required(VERSION 3.26)

# Give access to our `./cmake` local folder for `include(...)`.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Use `--format=gnu` to generate identical archives regardless of the host.
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> --format=gnu qc <TARGET> <OBJECTS>")

# If no `CMAKE_BUILD_TYPE` was specified, default to `Release`.
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type to use" FORCE)
endif()

set(TARGET_TRIPLE "wasm32-wasip1" CACHE STRING "WASI target to test")
set(CMAKE_C_COMPILER_TARGET ${TARGET_TRIPLE})

# Force a cross-compile to WASI or WEB at all times.
if(TARGET_TRIPLE MATCHES "-wasi")
  set(CMAKE_SYSTEM_NAME WASI)
elseif(TARGET_TRIPLE MATCHES "-web")
  set(CMAKE_SYSTEM_NAME WEB)
else()
  message(FATAL_ERROR "Unknown target triple: ${TARGET_TRIPLE}")
endif()

# I'm not entirely sure why this is necessary on Windows. This may be a bug in
# CMake, I'm not sure. The problem is that, at least on CI, CMake detects that
# the compiler in use is clang but doesn't understand the `SYSTEM` header
# option meaning it just pass `-I...` instead of
# `-isystem ...`. This in turn causes the build to fail for... somewhat
# unknown reasons which may also be bugs in wasi-libc. Setting this seems
# to work around the issue however.
if (CMAKE_HOST_WIN32)
  set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-isystem ")
endif()

# We're building libc, and the compiler being used may not have a wasm sysroot
# for libc, so skip this check. Otherwise the `project` call below attempts a
# link to see if the compiler is working and if that fails it doesn't mean it
# doesn't work. Let the build keep going to see what happens.
set(CMAKE_C_COMPILER_WORKS 1)

project(wasi-libc LANGUAGES C ASM)

if(NOT CMAKE_C_COMPILER_ID MATCHES Clang)
  message(FATAL_ERROR "C compiler ${CMAKE_C_COMPILER} is not `Clang`, it is ${CMAKE_C_COMPILER_ID}")
endif()

message(STATUS "Found executable for `nm`: ${CMAKE_NM}")
message(STATUS "Found executable for `ar`: ${CMAKE_AR}")
message(STATUS "Found executable for `ranlib`: ${CMAKE_RANLIB}")

# Note: thin LTO here is just for experimentation. It has known issues:
# - https://github.com/llvm/llvm-project/issues/91700
# - https://github.com/llvm/llvm-project/issues/91711
set(LTO "" CACHE STRING "LTO mode to use")
set(MALLOC "dlmalloc" CACHE STRING "Malloc implementation to use (dlmalloc, emmalloc, or none)")
set(BUILTINS_LIB "" CACHE STRING "Path to precompiled compiler-rt builtins library")
set(BULK_MEMORY_THRESHOLD "32" CACHE STRING "Threshold to compile code with")
option(SETJMP "Build setjmp/longjmp support" ON)
option(BUILD_TESTS "Whether or not to build tests" OFF)
option(SIMD "Whether or not to build simd-enabled intrinsics into wasi-libc" OFF)
option(BUILD_SHARED "Whether or not to build shared libraries" ON)
option(CHECK_SYMBOLS "Whether or not to check the exported symbols of libc.a" OFF)
set(WASI_SDK_VERSION "" CACHE STRING "Version information for wasi-sdk to embed in headers")
option(ENABLE_WERROR "Whether to compile with `-Werror`" OFF)
option(ENABLE_COOP_THREADS "Whether to compile with cooperative threads support" OFF)
option(LOG_UNSUPPORTED_CALLS "Whether to log error messages when calling unsupported APIs" OFF)

if(TARGET_TRIPLE MATCHES "-threads$")
  set(WASI p1)
  # TODO: As of this writing, wasi_thread_start.s uses non-position-independent
  # code, and I'm not sure how to make it position-independent.  Once we've
  # done that, we can enable libc.so for the wasi-threads build.
  set(SHARED OFF)
elseif(TARGET_TRIPLE MATCHES "-wasi$")
  set(WASI p1)
  set(SHARED ON)
elseif(TARGET_TRIPLE MATCHES "-wasip1$")
  set(WASI p1)
  set(SHARED ON)
elseif(TARGET_TRIPLE MATCHES "-wasip2$")
  set(WASI p2)
  set(SHARED ON)
elseif(TARGET_TRIPLE MATCHES "-wasip3$")
  set(WASI p3)
  set(SHARED ON)
elseif(TARGET_TRIPLE MATCHES "-webp2$")
  set(WEB p2)
  set(SHARED ON)
else()
  message(FATAL_ERROR "Unknown target triple: ${TARGET_TRIPLE}")
endif()

if(BUILD_SHARED)
  if (NOT SHARED)
    message(WARNING "Disabling shared library builds as target is incompatible with shared libs")
  endif()
else()
  if (SHARED)
    message(STATUS "Disabling shared library builds as BUILD_SHARED=OFF")
  endif()
  set(SHARED OFF)
endif()

if(TARGET_TRIPLE MATCHES "-threads$")
  add_compile_options(-mthread-model posix -pthread -ftls-model=local-exec -matomics)
elseif(ENABLE_COOP_THREADS)
  add_compile_options(-mthread-model posix -pthread -ftls-model=local-exec)
else()
  add_compile_options(-mthread-model single)
endif()

if(MALLOC STREQUAL "dlmalloc")
  message(STATUS "Using dlmalloc as the malloc implementation")
  set(malloc_target dlmalloc)
elseif(MALLOC STREQUAL "emmalloc")
  message(STATUS "Using emmalloc as the malloc implementation")
  set(malloc_target emmalloc)
elseif(MALLOC STREQUAL "none")
  message(STATUS "Using nothing as the malloc implementation")
else()
  message(FATAL_ERROR "Unsupported malloc implementation: ${MALLOC}")
endif()

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
                OUTPUT_VARIABLE clang_version
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(SYSROOT ${CMAKE_CURRENT_BINARY_DIR}/sysroot)
set(SYSROOT_INC ${SYSROOT}/include/${TARGET_TRIPLE})
set(SYSROOT_LIB ${SYSROOT}/lib/${TARGET_TRIPLE})

add_custom_target(sysroot ALL)
add_custom_target(sysroot_inc)

if(LTO)
  message(STATUS "Enabling LTO: ${LTO}")
  set(SYSROOT_LIB ${SYSROOT_LIB}/llvm-lto/${clang_version})
  if(LTO STREQUAL "thin")
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
  elseif(LTO STREQUAL "full")
    add_compile_options(-flto)
    add_link_options(-flto)
  else()
    message(FATAL_ERROR "Unknown LTO mode: ${LTO}")
  endif()
endif()

if(SIMD)
  add_compile_options(-msimd128 -mrelaxed-simd -mbulk-memory
    -D__wasilibc_simd_string)
endif()

if(LOG_UNSUPPORTED_CALLS)
  add_compile_definitions(WASILIBC_LOG_UNSUPPORTED_CALLS)
endif()

if(WASI STREQUAL "p1")
  set(__wasip1__ ON)
elseif(WASI STREQUAL "p2")
  set(__wasip2__ ON)
elseif(WASI STREQUAL "p3")
  set(__wasip3__ ON)
elseif(WEB STREQUAL "p2")
  set(__webp2__ ON)
else()
  message(FATAL_ERROR "Unknown WASI version: ${WASI}")
endif()

set(wasip2-version 0.2.12)
set(wasip3-version 0.3.2)
set(webp2-version 0.2.0)

# =============================================================================
# Detection of a libcall thread context for wasip3

include(CheckSourceCompiles)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
check_source_compiles(C [[
  #ifndef __wasm_libcall_thread_context__
  #error "missing define"
  #endif
]] HAVE_WASM_LIBCALL_THREAD_CONTEXT)

if(ENABLE_COOP_THREADS)
  if(NOT WASI STREQUAL "p3")
    message(FATAL_ERROR "Cooperative threads are only supported on WASIp3")
  endif()
  if (NOT HAVE_WASM_LIBCALL_THREAD_CONTEXT)
    message(FATAL_ERROR "Cooperative threads require clang 23 or later")
  endif()
  set(__wasi_cooperative_threads__ ON)
else()
endif()

include(bindings)
include(builtins)
if (NOT (WASI STREQUAL "p1"))
  include(wasm-component-ld)
endif()
include(check-symbols)
include(clang-format)
include(wasm-tools)
include(wasi-wits)

# =============================================================================
# Generic top-level build flags/settings
#
add_compile_options(--target=${TARGET_TRIPLE})

add_link_options(
  --target=${TARGET_TRIPLE}
  --sysroot=${SYSROOT}

  # By default clang will try to find, locate, and link compiler-rt. To get
  # this to work a `-resource-dir` argument is passed to ensure that our
  # custom `tmp_resource_dir` built here locally is used instead of the system
  # directory which may or may not already have compiler-rt for wasm.
  -resource-dir ${tmp_resource_dir}
)

if(__webp2__)
  # TODO(webp2): Manually instruct clang to look at our new webp2 sysroot until
  # it has been patched in wasi-sdk to know about "webp2" as an OS.
  add_link_options(-B${SYSROOT_LIB} -L${SYSROOT_LIB})
endif()

# Expose the public headers to the implementation. We use `-isystem` for
# purpose for two reasons:
#
# 1. It only does `<...>` not `"...."` lookup. We are making a libc,
#    which is a system library, so all public headers should be
#    accessible via `<...>` and never also need `"..."`. `-isystem` main
#    purpose is to only effect `<...>` lookup.
#
# 2. The `-I` for private headers added for specific C files below
#    should come earlier in the search path, so they can "override"
#    and/or `#include_next` the public headers. `-isystem` (like
#    `-idirafter`) comes later in the search path than `-I`.
# add_compile_options(-isystem ${SYSROOT_INC})
include_directories(SYSTEM ${SYSROOT_INC})

include_directories(libc-bottom-half/cloudlibc/src)


add_compile_options(
  # WebAssembly floating-point match doesn't trap.
  # TODO: Add -fno-signaling-nans when the compiler supports it.
  -fno-trapping-math

  # Add all warnings. Note that third-party code is compiled with extra flags
  # to disable warnings that crop up.
  -Wall -Wextra

  # Compiling assembly flags lots of arguments as unused but that's not too
  # interesting so just ignore that.
  $<$<COMPILE_LANGUAGE:ASM>:-Wno-unused-command-line-argument>
)

if (ENABLE_WERROR)
  add_compile_options(-Werror)
endif()

set(WASILIBC_MULTITHREADING OFF)
if (ENABLE_COOP_THREADS OR TARGET_TRIPLE MATCHES "-threads")
  set(WASILIBC_MULTITHREADING ON)
endif()

# In wasip2/wasip3 code we're using `defer { ... }`, so make sure that exists.
include(CheckCCompilerFlag)
check_c_compiler_flag(-fdefer-ts HAVE_FDEFER_TS)
if(HAVE_FDEFER_TS)
  add_compile_options(-fdefer-ts)
elseif(NOT WASI STREQUAL "p1")
  message(FATAL_ERROR "wasip2/wasip3/webp2 require clang 22 or later for `-fdefer-ts`")
endif()

# =============================================================================
# Helper functions for adding libraries.

# Helper to add the `src` file to the sysroot at `dst`. Adds to the `sysroot_inc`
# list of dependencies.
function(add_sysroot_header src dst)
  string(REPLACE "/" "_" target_name ${dst})
  add_custom_command(
    OUTPUT ${SYSROOT_INC}/${dst}
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_CURRENT_SOURCE_DIR}/${src}
      ${SYSROOT_INC}/${dst}
    DEPENDS ${src}
  )
  add_custom_target(sysroot-header-${target_name} DEPENDS ${SYSROOT_INC}/${dst})
  add_dependencies(sysroot_inc sysroot-header-${target_name})
  clang_format_file(${src})
endfunction()

# Adds two object libraries named `${name}-{shared,static}` and hook them up to
# the sysroot. The `*-shared` library is compiled with `-fPIC`.
function(add_object_library name)
  add_library(${name}-shared OBJECT EXCLUDE_FROM_ALL ${ARGN})
  set_pic(${name}-shared)
  target_compile_options(${name}-shared PRIVATE -fvisibility=default)
  add_dependencies(${name}-shared sysroot_inc)
  clang_format_target(${name}-shared)

  add_library(${name}-static OBJECT EXCLUDE_FROM_ALL ${ARGN})
  add_dependencies(${name}-static sysroot_inc)
endfunction()

# Same as `add_object_library`, but also adds dependencies on
# `musl-top-half-interface` for compiler flags and include dirs.
function(add_internal_object_library name)
  add_object_library(${name} ${ARGN})
  target_link_libraries(${name}-shared PUBLIC musl-top-half-interface)
  target_link_libraries(${name}-static PUBLIC musl-top-half-interface)
endfunction()

# Adds a shared library `name` with the following arguments as the
# sources.
#
# Adds a dependnecy on `musl-top-half-interface` for compiler flags,
# `sysroot-c` and `builtins` to ensure link-time dependencies are met,
# and then ensures compile options are configured.
function(add_internal_shared_library name)
  add_library(${name} SHARED EXCLUDE_FROM_ALL ${ARGN})
  set_pic(${name})
  target_link_libraries(${name} PUBLIC musl-top-half-interface)
  add_dependencies(${name} sysroot-c builtins sysroot-empty-pthread)
  target_compile_options(${name} PRIVATE -fvisibility=default)
  set_target_properties(${name} PROPERTIES NO_SONAME 1)
  clang_format_target(${name})
endfunction()

# Adds `${name}` and `${name}-static.
#
# Wrapper around `add_internal_shared_library` and then adding
# a static library as well.
function(add_internal_library_pair name)
  add_internal_shared_library(${name} ${ARGN})

  add_library(${name}-static STATIC EXCLUDE_FROM_ALL ${ARGN})
  target_link_libraries(${name}-static PUBLIC musl-top-half-interface)
endfunction()

# Helper to add `target`, a cmake library target, to the sysroot at `file`.
#
# Copies the output into place and adds it to the top-level `sysroot`
# target.
function(sysroot_lib target file)
  set(dst ${SYSROOT_LIB}/${file})
  add_custom_command(
    OUTPUT ${dst}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${dst}
    DEPENDS ${target}
  )
  add_custom_target(sysroot-${target} DEPENDS ${dst})
  if (NOT (${file} MATCHES "\\.so$") OR SHARED)
    add_dependencies(sysroot sysroot-${target})
  endif()
endfunction()

function(set_pic target)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
  # Windows needs an extra nudge to pass `-fPIC`
  if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    target_compile_options(${target} PRIVATE -fPIC)
  endif()
endfunction()

# =============================================================================
# subdirectories
#

add_subdirectory(dlmalloc)
add_subdirectory(emmalloc)
add_subdirectory(fts)
add_subdirectory(libc-bottom-half)
add_subdirectory(libc-top-half)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()

#==============================================================================
# libc.{a,so}
#

# TODO: if this `*.o` file is added to the `bottom-half-*` object libraries it
# doesn't make its way to the linker. I don't know if that's a bug in cmake or
# me not understanding cmake. This should ideally be bottom-half logic but it's
# here since I can't figure out anything else that works for now.
if (WASI STREQUAL "p2")
  set(libc_extra_objects libc-bottom-half/sources/wasip2_component_type.o)
elseif (WASI STREQUAL "p3")
  set(libc_extra_objects libc-bottom-half/sources/wasip3_component_type.o)
elseif (WEB STREQUAL "p2")
  set(libc_extra_objects libc-bottom-half/sources/webp2_component_type.o)
endif()

add_library(c SHARED EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:bottom-half-shared>
  $<TARGET_OBJECTS:top-half-shared>
  $<TARGET_OBJECTS:printscan-longdouble-shared>
  $<TARGET_OBJECTS:${malloc_target}-shared>
  $<TARGET_OBJECTS:bulk-memory-shared>
  $<TARGET_OBJECTS:fts-shared>
  ${libc_extra_objects}
)
add_library(c-static STATIC EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:bottom-half-static>
  $<TARGET_OBJECTS:top-half-static>
  $<TARGET_OBJECTS:printscan-default-static>
  $<TARGET_OBJECTS:${malloc_target}-static>
  $<TARGET_OBJECTS:bulk-memory-static>
  $<TARGET_OBJECTS:fts-static>
  ${libc_extra_objects}
)
add_dependencies(c sysroot-startup-objects builtins)

target_link_options(c PRIVATE
  # Note: libc.so is special because it shouldn't link to libc.so, and the
  # -nodefaultlibs flag here disables the default `-lc` logic that clang has.
  # Note though that this also disables linking of compiler-rt libraries so
  # that is explicitly passed in via `${builtins_lib_path}`
  -nodefaultlibs
  ${builtins_lib_path}

  # Note: --allow-undefined-file=linker-provided-symbols.txt is a workaround
  # for https://github.com/llvm/llvm-project/issues/103592
  -Wl,--allow-undefined-file=${CMAKE_CURRENT_SOURCE_DIR}/linker-provided-symbols.txt
)

# TODO: Specify SDK version, e.g. libc.so.wasi-sdk-21, as SO_NAME once `wasm-ld`
# supports it.
set_target_properties(c PROPERTIES NO_SONAME 1)

sysroot_lib(c-static libc.a)
sysroot_lib(c libc.so)

# =============================================================================
# Empty archives in the sysroot to satisfy `-l` flags

function(empty_sysroot_archive file)
  set(dst ${SYSROOT_LIB}/lib${file}.a)
  add_custom_command(
    OUTPUT ${dst}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
    COMMAND ${CMAKE_AR} crs ${dst}
  )
  add_custom_target(sysroot-empty-${file} DEPENDS ${dst})
  add_dependencies(sysroot sysroot-empty-${file})
endfunction()

empty_sysroot_archive(crypt)
empty_sysroot_archive(m)
empty_sysroot_archive(pthread)
empty_sysroot_archive(resolv)
empty_sysroot_archive(rt)
empty_sysroot_archive(util)
empty_sysroot_archive(xnet)

# =============================================================================
# Installation
#
include(GNUInstallDirs)
install(DIRECTORY ${SYSROOT}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${SYSROOT}/lib/ DESTINATION ${CMAKE_INSTALL_LIBDIR})
