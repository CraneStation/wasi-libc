add_subdirectory(clocks)
add_subdirectory(mman)
add_subdirectory(getpid)
add_subdirectory(signal)

# =============================================================================
# sysroot headers from the bottom half
#
if(WASI_SDK_VERSION)
  set(__wasi_sdk_version__ ${WASI_SDK_VERSION})
  if (WASI_SDK_VERSION MATCHES "([0-9]+)\\..*")
    set(__wasi_sdk_major__ ${CMAKE_MATCH_1})
  endif()
endif()
configure_file(headers/public/wasi/version.h.in ${SYSROOT_INC}/wasi/version.h)
add_custom_target(sysroot-wasi-snapshot-header DEPENDS ${SYSROOT_INC}/wasi/version.h)
add_dependencies(sysroot_inc sysroot-wasi-snapshot-header)

file(
  GLOB_RECURSE globbed_headers
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  CONFIGURE_DEPENDS
  headers/public/*.h
)

if (NOT (WASI STREQUAL "p2"))
  list(REMOVE_ITEM globbed_headers headers/public/wasi/__generated_wasip2.h)
endif()

if (NOT (WASI STREQUAL "p3"))
  list(REMOVE_ITEM globbed_headers headers/public/wasi/__generated_wasip3.h)
endif()

if (NOT (WEB STREQUAL "p2"))
  list(REMOVE_ITEM globbed_headers headers/public/wasi/__generated_webp2.h)
endif()

foreach(header ${globbed_headers})
  string(REPLACE "headers/public/" "" dst ${header})
  add_sysroot_header(${header} ${dst})
endforeach()

file(
  GLOB_RECURSE globbed_private_headers
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  CONFIGURE_DEPENDS
  headers/private/*.h
)

foreach(header ${globbed_private_headers})
  clang_format_file(${header})
endforeach()

# =============================================================================
# building bottom-half sources
#

set(bottom_half_sources
  cloudlibc/src/libc/dirent/closedir.c
  cloudlibc/src/libc/dirent/dirfd.c
  cloudlibc/src/libc/dirent/fdclosedir.c
  cloudlibc/src/libc/dirent/fdopendir.c
  cloudlibc/src/libc/dirent/opendirat.c
  cloudlibc/src/libc/dirent/readdir.c
  cloudlibc/src/libc/dirent/rewinddir.c
  cloudlibc/src/libc/dirent/scandirat.c
  cloudlibc/src/libc/dirent/seekdir.c
  cloudlibc/src/libc/dirent/telldir.c
  cloudlibc/src/libc/errno/errno.c
  cloudlibc/src/libc/fcntl/fcntl.c
  cloudlibc/src/libc/fcntl/openat.c
  cloudlibc/src/libc/fcntl/posix_fadvise.c
  cloudlibc/src/libc/fcntl/posix_fallocate.c
  cloudlibc/src/libc/poll/poll.c
  cloudlibc/src/libc/sched/sched_yield.c
  cloudlibc/src/libc/stdio/renameat.c
  cloudlibc/src/libc/stdlib/_Exit.c
  cloudlibc/src/libc/sys/ioctl/ioctl.c
  cloudlibc/src/libc/sys/select/pselect.c
  cloudlibc/src/libc/sys/select/select.c
  cloudlibc/src/libc/sys/stat/fstat.c
  cloudlibc/src/libc/sys/stat/fstatat.c
  cloudlibc/src/libc/sys/stat/futimens.c
  cloudlibc/src/libc/sys/stat/mkdirat.c
  cloudlibc/src/libc/sys/stat/utimensat.c
  cloudlibc/src/libc/sys/time/gettimeofday.c
  cloudlibc/src/libc/sys/uio/preadv.c
  cloudlibc/src/libc/sys/uio/pwritev.c
  cloudlibc/src/libc/sys/uio/readv.c
  cloudlibc/src/libc/sys/uio/writev.c
  cloudlibc/src/libc/time/CLOCK_MONOTONIC.c
  cloudlibc/src/libc/time/CLOCK_REALTIME.c
  cloudlibc/src/libc/time/clock_getres.c
  cloudlibc/src/libc/time/clock_gettime.c
  cloudlibc/src/libc/time/clock_nanosleep.c
  cloudlibc/src/libc/time/nanosleep.c
  cloudlibc/src/libc/time/time.c
  cloudlibc/src/libc/unistd/faccessat.c
  cloudlibc/src/libc/unistd/fdatasync.c
  cloudlibc/src/libc/unistd/fsync.c
  cloudlibc/src/libc/unistd/ftruncate.c
  cloudlibc/src/libc/unistd/linkat.c
  cloudlibc/src/libc/unistd/lseek.c
  cloudlibc/src/libc/unistd/pread.c
  cloudlibc/src/libc/unistd/pwrite.c
  cloudlibc/src/libc/unistd/read.c
  cloudlibc/src/libc/unistd/readlinkat.c
  cloudlibc/src/libc/unistd/sleep.c
  cloudlibc/src/libc/unistd/symlinkat.c
  cloudlibc/src/libc/unistd/unlinkat.c
  cloudlibc/src/libc/unistd/usleep.c
  cloudlibc/src/libc/unistd/write.c
  sources/__errno_location.c
  sources/__main_void.c
  sources/__wasilibc_dt.c
  sources/__wasilibc_environ.c
  sources/__wasilibc_fd_renumber.c
  sources/__wasilibc_initialize_environ.c
  sources/__wasilibc_random.c
  sources/__wasilibc_real.c
  sources/__wasilibc_report_error.c
  sources/__wasilibc_rmdirat.c
  sources/__wasilibc_tell.c
  sources/__wasilibc_unlinkat.c
  sources/abort.c
  sources/at_fdcwd.c
  sources/chdir.c
  sources/complex-builtins.c
  sources/dup.c
  sources/environ.c
  sources/errno.c
  sources/getcwd.c
  sources/getentropy.c
  sources/isatty.c
  sources/math/fmin-fmax.c
  sources/math/math-builtins.c
  sources/pipe.c
  sources/posix.c
  sources/ppoll.c
  sources/preopens.c
  sources/reallocarray.c
  sources/sbrk.c
  sources/truncate.c
  sources/utf8.c
  sources/__cabi_realloc_wrapper.S
)

if (WASI STREQUAL "p1")
  list(APPEND bottom_half_sources
    cloudlibc/src/libc/sys/socket/getsockopt.c
    cloudlibc/src/libc/sys/socket/recv.c
    cloudlibc/src/libc/sys/socket/send.c
    cloudlibc/src/libc/sys/socket/shutdown.c
    sources/accept-wasip1.c
  )
else()
  list(APPEND bottom_half_sources
    sources/accept.c
    sources/bind.c
    sources/connect.c
    sources/descriptor_table.c
    sources/file_utils.c
    sources/getsockpeername.c
    sources/listen.c
    sources/recv.c
    sources/send.c
    sources/shutdown.c
    sources/socket.c
    sources/sockopt.c
  )
endif()

if (WASI STREQUAL "p2" OR WASI STREQUAL "p3")
  list(APPEND bottom_half_sources
    sources/file.c
    sources/netdb.c
    sources/sockets_utils.c
    sources/tcp.c
    sources/udp.c
  )
endif()

# wasip2-specific
if(WASI STREQUAL "p2")
  list(APPEND bottom_half_sources
    sources/wasip2.c
    sources/wasip2_stdio.c
  )
endif()

# wasip3-specific
if (WASI STREQUAL "p3")
  list(APPEND bottom_half_sources
    sources/wasip3.c
    sources/wasip3_block_on.c
    sources/wasip3_stdio.c
  )
endif()

# webp2-specific
if (WEB STREQUAL "p2")
  list(APPEND bottom_half_sources
    sources/webp2.c
    sources/webp2_stdio.c
  )
endif()

if (HAVE_WASM_LIBCALL_THREAD_CONTEXT)
  list(APPEND bottom_half_sources
    sources/__wasm_task_hook.S
    sources/wasip3_tls.c
    sources/task_context.c
  )
endif()

# Don't export symbols of generated code in shared libraries, so specifically
# pass `-fvisibility=hidden`.
set_source_files_properties(sources/wasip2.c PROPERTIES
  COMPILE_OPTIONS -fvisibility=hidden)
set_source_files_properties(sources/wasip3.c PROPERTIES
  COMPILE_OPTIONS -fvisibility=hidden)
set_source_files_properties(sources/webp2.c PROPERTIES
  COMPILE_OPTIONS -fvisibility=hidden)

add_object_library(bottom-half ${bottom_half_sources})
foreach(obj bottom-half-shared bottom-half-static)
  target_link_libraries(${obj} PUBLIC musl-top-half-interface)
  target_include_directories(${obj} PRIVATE
    cloudlibc/src/include
    cloudlibc/src
    headers/private
  )
endforeach()


# =============================================================================
# startup objects
#
# This is the logic for building `crt1{,-reactor,-command}.o` objects which
# Clang will link by default based on compiler flags. These are compiled into
# "object libraries" with CMake and then they're copied into the final location
# with adjustments based on WASI versions.

set(crt_sources crt/crt1-reactor.c crt/crt1-command.c)

foreach(file ${crt_sources})
  cmake_path(GET file STEM stem)
  add_library(${stem} OBJECT ${file})
  clang_format_target(${stem})
  target_link_libraries(${stem} PRIVATE musl-top-half-interface)
  set_pic(${stem})
  target_compile_options(${stem} PRIVATE -fvisibility=default -fno-lto)
  target_include_directories(${stem} PRIVATE headers/private)
endforeach()

set(crt_sysroot ${SYSROOT}/lib/${TARGET_TRIPLE})

# crt1-reactor.o is a straight copy of what CMake produces
add_custom_command(
  OUTPUT ${crt_sysroot}/crt1-reactor.o
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-reactor> ${crt_sysroot}/crt1-reactor.o
  DEPENDS crt1-reactor $<TARGET_OBJECTS:crt1-reactor>
)

if (WASI STREQUAL "p1" OR WEB STREQUAL "p2")
  # crt1-command.o is a straight copy of what CMake produces
  add_custom_command(
    OUTPUT ${crt_sysroot}/crt1-command.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-command> ${crt_sysroot}/crt1-command.o
    DEPENDS crt1-command $<TARGET_OBJECTS:crt1-command>
  )
else()
  if (WASI STREQUAL "p2")
    set(world wasi:cli/command@${wasip2-version})
  elseif (WASI STREQUAL "p3")
    set(world wasi:cli/command@${wasip3-version})
  else()
    message(FATAL_ERROR "Unknown WASI version: ${WASI}")
  endif()

  # wasip2+: crt1-command.o is modified from what CMake produces to
  # additionally have a custom section representing the type information needed
  # for its contained export. This is the `wasi:cli/run` interface, for example.
  #
  # TODO: ideally this wouldn't need `wasm-tools` as that's an extra build tool
  # needed here and it's best to slim down dependencies as much as possible.
  # This additionally requires downloading/vendoring WITs which isn't great.
  # Ideally the type information embedded here would be referenced dierctly in
  # `crt1-command.c` itself in the source. That would likely require some
  # combination of `__asm__` and `#embed` but I at least couldn't figure out how
  # to get that working. I'm also not aware of a way to, in Clang, define a
  # global in C that goes into a custom section in the output object. Another
  # possible route would be to use the `--relocatable` option of `wasm-ld,` but
  # it looks like that loses `__attribute__((export_name("...")))` information
  # which this object file relies on. As a workaround `wasm-tools` is used for
  # now. If you, dear reader, know of how to do this in the C source itself
  # please feel free to open an issue or a PR and maintainers can work with you
  # on getting that integrated.
  add_custom_command(
    OUTPUT ${crt_sysroot}/crt1-command.o
    COMMAND
      ${wasm_tools} component embed
        ${CMAKE_SOURCE_DIR}/wasi/${WASI}/wit
        $<TARGET_OBJECTS:crt1-command>
        --world ${world}
        -o ${crt_sysroot}/crt1-command.o
    DEPENDS crt1-command $<TARGET_OBJECTS:crt1-command> wasm-tools
  )
endif()

# Provide a plain crt1.o for toolchain compatibility, identical to
# `crt1-command.c`
add_custom_command(
  OUTPUT ${crt_sysroot}/crt1.o
  COMMAND ${CMAKE_COMMAND} -E copy ${crt_sysroot}/crt1-command.o ${crt_sysroot}/crt1.o
  DEPENDS ${crt_sysroot}/crt1-command.o
)

set(startup_objects
  ${crt_sysroot}/crt1-reactor.o
  ${crt_sysroot}/crt1-command.o
  ${crt_sysroot}/crt1.o
)
add_custom_target(sysroot-startup-objects DEPENDS ${startup_objects})
add_dependencies(sysroot sysroot-startup-objects)
