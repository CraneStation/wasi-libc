cmake_minimum_required(VERSION 3.12.1)

project(wasi-libc
  LANGUAGES C)
enable_testing()

set(CMAKE_C_COMPILE_OPTIONS_TARGET "-target ")
set(CMAKE_C_COMPILER_TARGET wasm32-unknown-wasi CACHE STRING
  "The target to compile for")

include(CMakeDependentOption)

option(ENABLE_THREADS "enable threads" OFF)

find_package(Python COMPONENTS Interpreter REQUIRED)

add_compile_options(--sysroot=${CMAKE_BINARY_DIR}/sysroot)

# WebAssembly floating point match doesn't trap
# TODO: add -fno-signaling-nans when the compiler supports it
add_compile_options(-fno-trapping-math)
if(ENABLE_THREADS)
  add_compile_options(-mthread-model posix -pthread)
else()
  add_compile_options(-mthread-model single)
endif()

# sysroot
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/alltypes.h.gen
  # TODO(compnerd) use ${CMAKE_SED}
  COMMAND sed -f ${PROJECT_SOURCE_DIR}/libc-top-half/musl/tools/mkalltypes.sed ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/alltypes.h.in ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/alltypes.h.in > ${CMAKE_BINARY_DIR}/alltypes.h.gen
  DEPENDS
    ${PROJECT_SOURCE_DIR}/libc-top-half/musl/tools/mkalltypes.sed
    ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/alltypes.h.in
    ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/alltypes.h.in)

add_custom_command(
  OUTPUT
    ${CMAKE_BINARY_DIR}/sysroot
    ${CMAKE_BINARY_DIR}/sysroot/share/include-all.c

  COMMAND
    ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_BINARY_DIR}/sysroot/include/
      ${CMAKE_BINARY_DIR}/sysroot/include/arpa/
      ${CMAKE_BINARY_DIR}/sysroot/include/bits/
      ${CMAKE_BINARY_DIR}/sysroot/include/netinet/
      ${CMAKE_BINARY_DIR}/sysroot/include/netpacket/
      ${CMAKE_BINARY_DIR}/sysroot/include/sys/
      ${CMAKE_BINARY_DIR}/sysroot/include/wasi/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/basics/include/__errno.h
      ${PROJECT_SOURCE_DIR}/basics/include/__functions_malloc.h
      ${PROJECT_SOURCE_DIR}/basics/include/__functions_memcpy.h
      ${PROJECT_SOURCE_DIR}/basics/include/__header_inttypes.h
      ${PROJECT_SOURCE_DIR}/basics/include/__macro_PAGESIZE.h
      ${PROJECT_SOURCE_DIR}/basics/include/__struct_stat.h
      ${PROJECT_SOURCE_DIR}/basics/include/__struct_timespec.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_blkcnt_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_blksize_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_clock_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_dev_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_gid_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_ino_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_mode_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_nlink_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_off_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_ssize_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_suseconds_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_time_t.h
      ${PROJECT_SOURCE_DIR}/basics/include/__typedef_uid_t.h

      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__errno_values.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__fd_set.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__function___isatty.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_bits_signal.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_dirent.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_fcntl.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_netinet_in.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_poll.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_stdlib.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_string.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_sys_ioctl.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_sys_resource.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_sys_socket.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_sys_stat.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_time.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__header_unistd.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__macro_FD_SETSIZE.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__mode_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__seek.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_dirent.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_in6_addr.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_in_addr.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_iovec.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_msghdr.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_pollfd.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_rusage.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_sockaddr.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_sockaddr_in6.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_sockaddr_in.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_sockaddr_storage.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_sockaddr_un.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_timeval.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_tm.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__struct_tms.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_clockid_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_DIR.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_fd_set.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_in_addr_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_in_port_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_nfds_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_sa_family_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_sigset_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/__typedef_socklen_t.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/unistd.h

      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/alloca.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/ar.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/assert.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/byteswap.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/complex.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/cpio.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/crypt.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/ctype.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/dirent.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/endian.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/err.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/errno.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/fcntl.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/features.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/fenv.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/float.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/fmtmsg.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/fnmatch.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/ftw.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/getopt.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/glob.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/iconv.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/ifaddrs.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/inttypes.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/iso646.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/langinfo.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/libgen.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/limits.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/locale.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/malloc.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/math.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/memory.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/monetary.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/mqueue.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/nl_types.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/poll.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/regex.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sched.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/search.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/semaphore.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/signal.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdalign.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdbool.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdc-predef.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdint.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdio_ext.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdio.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdlib.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stdnoreturn.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/string.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/strings.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/stropts.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/syscall.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sysexits.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/tar.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/tgmath.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/threads.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/time.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/uchar.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/unistd.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/values.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/wchar.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/wctype.h

      $<$<BOOL:ENABLE_THREADS>:${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/aio.h>
      $<$<BOOL:ENABLE_THREADS>:${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/pthread.h>
    ${CMAKE_BINARY_DIR}/sysroot/include/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/ftp.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/inet.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/nameser_compat.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/nameser.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/telnet.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/arpa/tftp.h
    ${CMAKE_BINARY_DIR}/sysroot/include/arpa/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/fenv.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/hwcap.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/ioctl_fix.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/io.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/mman.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/poll.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/resource.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/generic/bits/termios.h

      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/endian.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/fcntl.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/float.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/ioctl.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/limits.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/posix.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/reg.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/signal.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/socket.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/stat.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/arch/wasm32/bits/stdint.h
    ${CMAKE_BINARY_DIR}/sysroot/include/bits/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/icmp6.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/igmp.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/in.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/in_systm.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/ip6.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/ip.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/ip_icmp.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/tcp.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netinet/udp.h
    ${CMAKE_BINARY_DIR}/sysroot/include/netinet/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/netpacket/packet.h
      ${CMAKE_BINARY_DIR}/sysroot/include/netpacket/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/dir.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/errno.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/eventfd.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/fcntl.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/file.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/ioctl.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/mman.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/param.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/poll.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/random.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/reg.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/resource.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/select.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/signal.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/socket.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/stat.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/stropts.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/syscall.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/sysinfo.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/timeb.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/time.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/times.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/timex.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/ttydefaults.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/types.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/uio.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/un.h
      ${PROJECT_SOURCE_DIR}/libc-top-half/musl/include/sys/utsname.h
    ${CMAKE_BINARY_DIR}/sysroot/include/sys/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/wasi/api.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/wasi/libc-find-relpath.h
      ${PROJECT_SOURCE_DIR}/libc-bottom-half/headers/public/wasi/libc.h
    ${CMAKE_BINARY_DIR}/sysroot/include/wasi/

  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_BINARY_DIR}/alltypes.h.gen
      ${CMAKE_BINARY_DIR}/sysroot/include/bits/alltypes.h

  COMMAND
    ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/sysroot/share

  COMMAND
    ${Python_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen-predefined-macros.py
      --cc ${CMAKE_C_COMPILER}
      --output-directory ${CMAKE_BINARY_DIR}/sysroot/share
      --sysroot ${CMAKE_BINARY_DIR}/sysroot
      -target ${CMAKE_C_COMPILER_TARGET} -O2 -DNDEBUG

  DEPENDS
    ${PROJECT_SOURCE_DIR}/tools/gen-predefined-macros.py
    ${CMAKE_BINARY_DIR}/alltypes.h.gen)
add_custom_target(sysroot-headers DEPENDS ${CMAKE_BINARY_DIR}/sysroot)

# startup files
add_library(crt1 OBJECT
  libc-bottom-half/crt/crt1.c)
add_dependencies(crt1 sysroot-headers)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/sysroot/lib/crt1.o
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/sysroot/lib
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_OBJECTS:crt1> ${CMAKE_BINARY_DIR}/sysroot/lib/crt1.o)
add_custom_target(crt1.o ALL
  DEPENDS ${CMAKE_BINARY_DIR}/sysroot/lib/crt1.o)
add_dependencies(crt1.o crt1)


add_library(c-printscan-no-floating-point
  libc-top-half/musl/src/internal/floatscan.c
  libc-top-half/musl/src/stdio/vfprintf.c
  libc-top-half/musl/src/stdio/vfwprintf.c
  libc-top-half/musl/src/stdio/vfscanf.c
  libc-top-half/musl/src/stdlib/strtod.c
  libc-top-half/musl/src/stdlib/wcstod.c)
set_target_properties(c-printscan-no-floating-point PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib)
target_compile_definitions(c-printscan-no-floating-point PRIVATE
  __wasilibc_printscan_no_floating_point
  __wasilibc_printscan_floating_point_support_option="remove -lc-printscan-no-floating-point from the link command")
target_compile_options(c-printscan-no-floating-point PRIVATE
  -Wno-parentheses
  -Wno-shift-op-parentheses
  -Wno-bitwise-op-parentheses
  -Wno-logical-op-parentheses
  -Wno-string-plus-int
  -Wno-dangling-else
  -Wno-unknown-pragmas)
target_include_directories(c-printscan-no-floating-point PRIVATE
  libc-top-half/musl/src/include
  libc-top-half/musl/src/internal
  libc-top-half/musl/arch/wasm32
  libc-top-half/musl/arch/generic
  libc-top-half/headers/private)
add_dependencies(c-printscan-no-floating-point sysroot-headers)


add_library(c-printscan-long-double
  libc-top-half/musl/src/internal/floatscan.c
  libc-top-half/musl/src/stdio/vfprintf.c
  libc-top-half/musl/src/stdio/vfwprintf.c
  libc-top-half/musl/src/stdio/vfscanf.c
  libc-top-half/musl/src/stdlib/strtod.c
  libc-top-half/musl/src/stdlib/wcstod.c)
set_target_properties(c-printscan-long-double PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib)
target_compile_options(c-printscan-long-double PRIVATE
  -Wno-parentheses
  -Wno-shift-op-parentheses
  -Wno-bitwise-op-parentheses
  -Wno-logical-op-parentheses
  -Wno-string-plus-int
  -Wno-dangling-else
  -Wno-unknown-pragmas)
target_include_directories(c-printscan-long-double PRIVATE
  libc-top-half/musl/src/include
  libc-top-half/musl/src/internal
  libc-top-half/musl/arch/wasm32
  libc-top-half/musl/arch/generic
  libc-top-half/headers/private)
add_dependencies(c-printscan-long-double sysroot-headers)


add_library(wasi-emulated-mman STATIC
  libc-bottom-half/mman/mman.c)
set_target_properties(wasi-emulated-mman PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib)
add_dependencies(wasi-emulated-mman sysroot-headers)


add_library(c
  basics/sources/abort.c
  basics/sources/complex-builtins.c
  basics/sources/reallocarray.c
  basics/sources/math/fmin-fmax.c
  basics/sources/math/math-builtins.c)
set_target_properties(c PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sysroot/lib)
add_dependencies(c sysroot-headers)

add_library(dlmalloc OBJECT
  dlmalloc/src/dlmalloc.c)
target_include_directories(dlmalloc PRIVATE
  dlmalloc/include)
add_dependencies(dlmalloc sysroot-headers)

add_library(libc-bottom-half OBJECT
  libc-bottom-half/cloudlibc/src/libc/stdlib/_Exit.c
  libc-bottom-half/cloudlibc/src/libc/sched/sched_yield.c
  libc-bottom-half/cloudlibc/src/libc/unistd/usleep.c
  libc-bottom-half/cloudlibc/src/libc/unistd/pwrite.c
  libc-bottom-half/cloudlibc/src/libc/unistd/ftruncate.c
  libc-bottom-half/cloudlibc/src/libc/unistd/write.c
  libc-bottom-half/cloudlibc/src/libc/unistd/symlinkat.c
  libc-bottom-half/cloudlibc/src/libc/unistd/unlinkat.c
  libc-bottom-half/cloudlibc/src/libc/unistd/close.c
  libc-bottom-half/cloudlibc/src/libc/unistd/lseek.c
  libc-bottom-half/cloudlibc/src/libc/unistd/faccessat.c
  libc-bottom-half/cloudlibc/src/libc/unistd/fsync.c
  libc-bottom-half/cloudlibc/src/libc/unistd/read.c
  libc-bottom-half/cloudlibc/src/libc/unistd/linkat.c
  libc-bottom-half/cloudlibc/src/libc/unistd/sleep.c
  libc-bottom-half/cloudlibc/src/libc/unistd/pread.c
  libc-bottom-half/cloudlibc/src/libc/unistd/fdatasync.c
  libc-bottom-half/cloudlibc/src/libc/unistd/readlinkat.c
  libc-bottom-half/cloudlibc/src/libc/sys/ioctl/ioctl.c
  libc-bottom-half/cloudlibc/src/libc/sys/time/gettimeofday.c
  libc-bottom-half/cloudlibc/src/libc/sys/socket/shutdown.c
  libc-bottom-half/cloudlibc/src/libc/sys/socket/getsockopt.c
  libc-bottom-half/cloudlibc/src/libc/sys/socket/recv.c
  libc-bottom-half/cloudlibc/src/libc/sys/socket/send.c
  libc-bottom-half/cloudlibc/src/libc/sys/select/select.c
  libc-bottom-half/cloudlibc/src/libc/sys/select/pselect.c
  libc-bottom-half/cloudlibc/src/libc/sys/times/times.c
  libc-bottom-half/cloudlibc/src/libc/sys/uio/writev.c
  libc-bottom-half/cloudlibc/src/libc/sys/uio/preadv.c
  libc-bottom-half/cloudlibc/src/libc/sys/uio/readv.c
  libc-bottom-half/cloudlibc/src/libc/sys/uio/pwritev.c
  libc-bottom-half/cloudlibc/src/libc/sys/resource/getrusage.c
  libc-bottom-half/cloudlibc/src/libc/sys/stat/utimensat.c
  libc-bottom-half/cloudlibc/src/libc/sys/stat/fstatat.c
  libc-bottom-half/cloudlibc/src/libc/sys/stat/fstat.c
  libc-bottom-half/cloudlibc/src/libc/sys/stat/futimens.c
  libc-bottom-half/cloudlibc/src/libc/sys/stat/mkdirat.c
  libc-bottom-half/cloudlibc/src/libc/fcntl/fcntl.c
  libc-bottom-half/cloudlibc/src/libc/fcntl/openat.c
  libc-bottom-half/cloudlibc/src/libc/fcntl/posix_fallocate.c
  libc-bottom-half/cloudlibc/src/libc/fcntl/posix_fadvise.c
  libc-bottom-half/cloudlibc/src/libc/errno/errno.c
  libc-bottom-half/cloudlibc/src/libc/time/clock.c
  libc-bottom-half/cloudlibc/src/libc/time/clock_gettime.c
  libc-bottom-half/cloudlibc/src/libc/time/CLOCK_PROCESS_CPUTIME_ID.c
  libc-bottom-half/cloudlibc/src/libc/time/clock_nanosleep.c
  libc-bottom-half/cloudlibc/src/libc/time/CLOCK_THREAD_CPUTIME_ID.c
  libc-bottom-half/cloudlibc/src/libc/time/CLOCK_REALTIME.c
  libc-bottom-half/cloudlibc/src/libc/time/clock_getres.c
  libc-bottom-half/cloudlibc/src/libc/time/time.c
  libc-bottom-half/cloudlibc/src/libc/time/nanosleep.c
  libc-bottom-half/cloudlibc/src/libc/time/CLOCK_MONOTONIC.c
  libc-bottom-half/cloudlibc/src/libc/dirent/closedir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/dirfd.c
  libc-bottom-half/cloudlibc/src/libc/dirent/opendirat.c
  libc-bottom-half/cloudlibc/src/libc/dirent/scandirat.c
  libc-bottom-half/cloudlibc/src/libc/dirent/readdir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/rewinddir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/fdopendir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/fdclosedir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/telldir.c
  libc-bottom-half/cloudlibc/src/libc/dirent/seekdir.c
  libc-bottom-half/cloudlibc/src/libc/poll/poll.c
  libc-bottom-half/cloudlibc/src/libc/stdio/renameat.c
  libc-bottom-half/libpreopen/libpreopen.c
  libc-bottom-half/sources/getentropy.c
  libc-bottom-half/sources/__environ.c
  libc-bottom-half/sources/__wasilibc_rmdirat.c
  libc-bottom-half/sources/errno.c
  libc-bottom-half/sources/__wasilibc_tell.c
  libc-bottom-half/sources/pause.c
  libc-bottom-half/sources/sbrk.c
  libc-bottom-half/sources/isatty.c
  libc-bottom-half/sources/__wasilibc_fd_renumber.c
  libc-bottom-half/sources/__wasilibc_unlinkat.c
  libc-bottom-half/sources/__original_main.c
  libc-bottom-half/sources/truncate.c)
target_include_directories(libc-bottom-half PRIVATE
  libc-bottom-half/headers/private
  libc-bottom-half/cloudlibc/src/include
  libc-bottom-half/cloudlibc/src)
add_dependencies(libc-bottom-half sysroot-headers)

add_library(libc-top-half OBJECT
  libc-top-half/musl/src/misc/a64l.c
  libc-top-half/musl/src/misc/basename.c
  libc-top-half/musl/src/misc/dirname.c
  libc-top-half/musl/src/misc/ffs.c
  libc-top-half/musl/src/misc/ffsl.c
  libc-top-half/musl/src/misc/ffsll.c
  libc-top-half/musl/src/misc/fmtmsg.c
  libc-top-half/musl/src/misc/getdomainname.c
  libc-top-half/musl/src/misc/gethostid.c
  libc-top-half/musl/src/misc/getopt.c
  libc-top-half/musl/src/misc/getopt_long.c
  libc-top-half/musl/src/misc/getsubopt.c
  libc-top-half/musl/src/misc/uname.c
  libc-top-half/musl/src/misc/nftw.c
  libc-top-half/musl/src/errno/strerror.c
  libc-top-half/musl/src/network/htonl.c
  libc-top-half/musl/src/network/htons.c
  libc-top-half/musl/src/network/ntohl.c
  libc-top-half/musl/src/network/ntohs.c
  libc-top-half/musl/src/network/inet_ntop.c
  libc-top-half/musl/src/network/inet_pton.c
  libc-top-half/musl/src/network/inet_aton.c
  libc-top-half/musl/src/network/in6addr_any.c
  libc-top-half/musl/src/network/in6addr_loopback.c
  libc-top-half/musl/src/fenv/fenv.c
  libc-top-half/musl/src/fenv/fesetround.c
  libc-top-half/musl/src/fenv/feupdateenv.c
  libc-top-half/musl/src/fenv/fesetexceptflag.c
  libc-top-half/musl/src/fenv/fegetexceptflag.c
  libc-top-half/musl/src/fenv/feholdexcept.c
  libc-top-half/musl/src/exit/exit.c
  libc-top-half/musl/src/exit/atexit.c
  libc-top-half/musl/src/exit/assert.c
  libc-top-half/musl/src/exit/quick_exit.c
  libc-top-half/musl/src/exit/at_quick_exit.c
  libc-top-half/musl/src/time/strftime.c
  libc-top-half/musl/src/time/asctime.c
  libc-top-half/musl/src/time/asctime_r.c
  libc-top-half/musl/src/time/ctime.c
  libc-top-half/musl/src/time/ctime_r.c
  libc-top-half/musl/src/time/wcsftime.c
  libc-top-half/musl/src/time/strptime.c
  libc-top-half/musl/src/time/difftime.c
  libc-top-half/musl/src/time/timegm.c
  libc-top-half/musl/src/time/ftime.c
  libc-top-half/musl/src/time/gmtime.c
  libc-top-half/musl/src/time/gmtime_r.c
  libc-top-half/musl/src/time/timespec_get.c
  libc-top-half/musl/src/time/getdate.c
  libc-top-half/musl/src/time/localtime.c
  libc-top-half/musl/src/time/localtime_r.c
  libc-top-half/musl/src/time/mktime.c
  libc-top-half/musl/src/time/__tm_to_secs.c
  libc-top-half/musl/src/time/__month_to_secs.c
  libc-top-half/musl/src/time/__secs_to_tm.c
  libc-top-half/musl/src/time/__year_to_secs.c
  libc-top-half/musl/src/time/__tz.c
  libc-top-half/musl/src/fcntl/creat.c
  libc-top-half/musl/src/dirent/alphasort.c
  libc-top-half/musl/src/dirent/versionsort.c
  libc-top-half/musl/src/env/clearenv.c
  libc-top-half/musl/src/env/getenv.c
  libc-top-half/musl/src/env/putenv.c
  libc-top-half/musl/src/env/setenv.c
  libc-top-half/musl/src/env/unsetenv.c
  libc-top-half/musl/src/unistd/posix_close.c
  libc-top-half/musl/src/internal/defsysinfo.c
  libc-top-half/musl/src/internal/floatscan.c
  libc-top-half/musl/src/internal/intscan.c
  libc-top-half/musl/src/internal/libc.c
  libc-top-half/musl/src/internal/shgetc.c
  libc-top-half/musl/src/stdio/asprintf.c
  libc-top-half/musl/src/stdio/clearerr.c
  libc-top-half/musl/src/stdio/dprintf.c
  libc-top-half/musl/src/stdio/ext2.c
  libc-top-half/musl/src/stdio/ext.c
  libc-top-half/musl/src/stdio/fclose.c
  libc-top-half/musl/src/stdio/__fclose_ca.c
  libc-top-half/musl/src/stdio/__fdopen.c
  libc-top-half/musl/src/stdio/feof.c
  libc-top-half/musl/src/stdio/ferror.c
  libc-top-half/musl/src/stdio/fflush.c
  libc-top-half/musl/src/stdio/fgetc.c
  libc-top-half/musl/src/stdio/fgetln.c
  libc-top-half/musl/src/stdio/fgetpos.c
  libc-top-half/musl/src/stdio/fgets.c
  libc-top-half/musl/src/stdio/fgetwc.c
  libc-top-half/musl/src/stdio/fgetws.c
  libc-top-half/musl/src/stdio/fileno.c
  libc-top-half/musl/src/stdio/fmemopen.c
  libc-top-half/musl/src/stdio/__fmodeflags.c
  libc-top-half/musl/src/stdio/fopen.c
  libc-top-half/musl/src/stdio/fopencookie.c
  libc-top-half/musl/src/stdio/__fopen_rb_ca.c
  libc-top-half/musl/src/stdio/fprintf.c
  libc-top-half/musl/src/stdio/fputc.c
  libc-top-half/musl/src/stdio/fputs.c
  libc-top-half/musl/src/stdio/fputwc.c
  libc-top-half/musl/src/stdio/fputws.c
  libc-top-half/musl/src/stdio/fread.c
  libc-top-half/musl/src/stdio/freopen.c
  libc-top-half/musl/src/stdio/fscanf.c
  libc-top-half/musl/src/stdio/fseek.c
  libc-top-half/musl/src/stdio/fsetpos.c
  libc-top-half/musl/src/stdio/ftell.c
  libc-top-half/musl/src/stdio/fwide.c
  libc-top-half/musl/src/stdio/fwprintf.c
  libc-top-half/musl/src/stdio/fwrite.c
  libc-top-half/musl/src/stdio/fwscanf.c
  libc-top-half/musl/src/stdio/getc.c
  libc-top-half/musl/src/stdio/getchar.c
  libc-top-half/musl/src/stdio/getchar_unlocked.c
  libc-top-half/musl/src/stdio/getc_unlocked.c
  libc-top-half/musl/src/stdio/getdelim.c
  libc-top-half/musl/src/stdio/getline.c
  libc-top-half/musl/src/stdio/getw.c
  libc-top-half/musl/src/stdio/getwc.c
  libc-top-half/musl/src/stdio/getwchar.c
  libc-top-half/musl/src/stdio/ofl_add.c
  libc-top-half/musl/src/stdio/ofl.c
  libc-top-half/musl/src/stdio/open_memstream.c
  libc-top-half/musl/src/stdio/open_wmemstream.c
  libc-top-half/musl/src/stdio/__overflow.c
  libc-top-half/musl/src/stdio/perror.c
  libc-top-half/musl/src/stdio/printf.c
  libc-top-half/musl/src/stdio/putc.c
  libc-top-half/musl/src/stdio/putchar.c
  libc-top-half/musl/src/stdio/putchar_unlocked.c
  libc-top-half/musl/src/stdio/putc_unlocked.c
  libc-top-half/musl/src/stdio/puts.c
  libc-top-half/musl/src/stdio/putw.c
  libc-top-half/musl/src/stdio/putwc.c
  libc-top-half/musl/src/stdio/putwchar.c
  libc-top-half/musl/src/stdio/rewind.c
  libc-top-half/musl/src/stdio/scanf.c
  libc-top-half/musl/src/stdio/setbuf.c
  libc-top-half/musl/src/stdio/setbuffer.c
  libc-top-half/musl/src/stdio/setlinebuf.c
  libc-top-half/musl/src/stdio/setvbuf.c
  libc-top-half/musl/src/stdio/snprintf.c
  libc-top-half/musl/src/stdio/sprintf.c
  libc-top-half/musl/src/stdio/sscanf.c
  libc-top-half/musl/src/stdio/stderr.c
  libc-top-half/musl/src/stdio/stdin.c
  libc-top-half/musl/src/stdio/__stdio_close.c
  libc-top-half/musl/src/stdio/__stdio_exit.c
  libc-top-half/musl/src/stdio/__stdio_read.c
  libc-top-half/musl/src/stdio/__stdio_seek.c
  libc-top-half/musl/src/stdio/__stdio_write.c
  libc-top-half/musl/src/stdio/stdout.c
  libc-top-half/musl/src/stdio/__stdout_write.c
  libc-top-half/musl/src/stdio/__string_read.c
  libc-top-half/musl/src/stdio/swprintf.c
  libc-top-half/musl/src/stdio/swscanf.c
  libc-top-half/musl/src/stdio/__toread.c
  libc-top-half/musl/src/stdio/__towrite.c
  libc-top-half/musl/src/stdio/__uflow.c
  libc-top-half/musl/src/stdio/ungetc.c
  libc-top-half/musl/src/stdio/ungetwc.c
  libc-top-half/musl/src/stdio/vasprintf.c
  libc-top-half/musl/src/stdio/vdprintf.c
  libc-top-half/musl/src/stdio/vfprintf.c
  libc-top-half/musl/src/stdio/vfscanf.c
  libc-top-half/musl/src/stdio/vfwprintf.c
  libc-top-half/musl/src/stdio/vfwscanf.c
  libc-top-half/musl/src/stdio/vprintf.c
  libc-top-half/musl/src/stdio/vscanf.c
  libc-top-half/musl/src/stdio/vsnprintf.c
  libc-top-half/musl/src/stdio/vsprintf.c
  libc-top-half/musl/src/stdio/vsscanf.c
  libc-top-half/musl/src/stdio/vswprintf.c
  libc-top-half/musl/src/stdio/vswscanf.c
  libc-top-half/musl/src/stdio/vwprintf.c
  libc-top-half/musl/src/stdio/vwscanf.c
  libc-top-half/musl/src/stdio/wprintf.c
  libc-top-half/musl/src/stdio/wscanf.c
  libc-top-half/musl/src/string/bcmp.c
  libc-top-half/musl/src/string/bcopy.c
  libc-top-half/musl/src/string/bzero.c
  libc-top-half/musl/src/string/explicit_bzero.c
  libc-top-half/musl/src/string/index.c
  libc-top-half/musl/src/string/memccpy.c
  libc-top-half/musl/src/string/memchr.c
  libc-top-half/musl/src/string/memcmp.c
  libc-top-half/musl/src/string/memcpy.c
  libc-top-half/musl/src/string/memmem.c
  libc-top-half/musl/src/string/memmove.c
  libc-top-half/musl/src/string/mempcpy.c
  libc-top-half/musl/src/string/memrchr.c
  libc-top-half/musl/src/string/memset.c
  libc-top-half/musl/src/string/rindex.c
  libc-top-half/musl/src/string/stpcpy.c
  libc-top-half/musl/src/string/stpncpy.c
  libc-top-half/musl/src/string/strcasecmp.c
  libc-top-half/musl/src/string/strcasestr.c
  libc-top-half/musl/src/string/strcat.c
  libc-top-half/musl/src/string/strchr.c
  libc-top-half/musl/src/string/strchrnul.c
  libc-top-half/musl/src/string/strcmp.c
  libc-top-half/musl/src/string/strcpy.c
  libc-top-half/musl/src/string/strcspn.c
  libc-top-half/musl/src/string/strdup.c
  libc-top-half/musl/src/string/strerror_r.c
  libc-top-half/musl/src/string/strlcat.c
  libc-top-half/musl/src/string/strlcpy.c
  libc-top-half/musl/src/string/strlen.c
  libc-top-half/musl/src/string/strncasecmp.c
  libc-top-half/musl/src/string/strncat.c
  libc-top-half/musl/src/string/strncmp.c
  libc-top-half/musl/src/string/strncpy.c
  libc-top-half/musl/src/string/strndup.c
  libc-top-half/musl/src/string/strnlen.c
  libc-top-half/musl/src/string/strpbrk.c
  libc-top-half/musl/src/string/strrchr.c
  libc-top-half/musl/src/string/strsep.c
  libc-top-half/musl/src/string/strspn.c
  libc-top-half/musl/src/string/strstr.c
  libc-top-half/musl/src/string/strtok.c
  libc-top-half/musl/src/string/strtok_r.c
  libc-top-half/musl/src/string/strverscmp.c
  libc-top-half/musl/src/string/swab.c
  libc-top-half/musl/src/string/wcpcpy.c
  libc-top-half/musl/src/string/wcpncpy.c
  libc-top-half/musl/src/string/wcscasecmp.c
  libc-top-half/musl/src/string/wcscasecmp_l.c
  libc-top-half/musl/src/string/wcscat.c
  libc-top-half/musl/src/string/wcschr.c
  libc-top-half/musl/src/string/wcscmp.c
  libc-top-half/musl/src/string/wcscpy.c
  libc-top-half/musl/src/string/wcscspn.c
  libc-top-half/musl/src/string/wcsdup.c
  libc-top-half/musl/src/string/wcslen.c
  libc-top-half/musl/src/string/wcsncasecmp.c
  libc-top-half/musl/src/string/wcsncasecmp_l.c
  libc-top-half/musl/src/string/wcsncat.c
  libc-top-half/musl/src/string/wcsncmp.c
  libc-top-half/musl/src/string/wcsncpy.c
  libc-top-half/musl/src/string/wcsnlen.c
  libc-top-half/musl/src/string/wcspbrk.c
  libc-top-half/musl/src/string/wcsrchr.c
  libc-top-half/musl/src/string/wcsspn.c
  libc-top-half/musl/src/string/wcsstr.c
  libc-top-half/musl/src/string/wcstok.c
  libc-top-half/musl/src/string/wcswcs.c
  libc-top-half/musl/src/string/wmemchr.c
  libc-top-half/musl/src/string/wmemcmp.c
  libc-top-half/musl/src/string/wmemcpy.c
  libc-top-half/musl/src/string/wmemmove.c
  libc-top-half/musl/src/string/wmemset.c
  libc-top-half/musl/src/locale/catclose.c
  libc-top-half/musl/src/locale/catgets.c
  libc-top-half/musl/src/locale/catopen.c
  libc-top-half/musl/src/locale/c_locale.c
  libc-top-half/musl/src/locale/duplocale.c
  libc-top-half/musl/src/locale/freelocale.c
  libc-top-half/musl/src/locale/iconv.c
  libc-top-half/musl/src/locale/iconv_close.c
  libc-top-half/musl/src/locale/langinfo.c
  libc-top-half/musl/src/locale/__lctrans.c
  libc-top-half/musl/src/locale/localeconv.c
  libc-top-half/musl/src/locale/locale_map.c
  libc-top-half/musl/src/locale/__mo_lookup.c
  libc-top-half/musl/src/locale/newlocale.c
  libc-top-half/musl/src/locale/pleval.c
  libc-top-half/musl/src/locale/setlocale.c
  libc-top-half/musl/src/locale/strcoll.c
  libc-top-half/musl/src/locale/strfmon.c
  libc-top-half/musl/src/locale/strxfrm.c
  libc-top-half/musl/src/locale/uselocale.c
  libc-top-half/musl/src/locale/wcscoll.c
  libc-top-half/musl/src/locale/wcsxfrm.c
  libc-top-half/musl/src/stdlib/abs.c
  libc-top-half/musl/src/stdlib/atof.c
  libc-top-half/musl/src/stdlib/atoi.c
  libc-top-half/musl/src/stdlib/atol.c
  libc-top-half/musl/src/stdlib/atoll.c
  libc-top-half/musl/src/stdlib/bsearch.c
  libc-top-half/musl/src/stdlib/div.c
  libc-top-half/musl/src/stdlib/ecvt.c
  libc-top-half/musl/src/stdlib/fcvt.c
  libc-top-half/musl/src/stdlib/gcvt.c
  libc-top-half/musl/src/stdlib/imaxabs.c
  libc-top-half/musl/src/stdlib/imaxdiv.c
  libc-top-half/musl/src/stdlib/labs.c
  libc-top-half/musl/src/stdlib/ldiv.c
  libc-top-half/musl/src/stdlib/llabs.c
  libc-top-half/musl/src/stdlib/lldiv.c
  libc-top-half/musl/src/stdlib/qsort.c
  libc-top-half/musl/src/stdlib/strtod.c
  libc-top-half/musl/src/stdlib/strtol.c
  libc-top-half/musl/src/stdlib/wcstod.c
  libc-top-half/musl/src/stdlib/wcstol.c
  libc-top-half/musl/src/search/hsearch.c
  libc-top-half/musl/src/search/insque.c
  libc-top-half/musl/src/search/lsearch.c
  libc-top-half/musl/src/search/tdelete.c
  libc-top-half/musl/src/search/tdestroy.c
  libc-top-half/musl/src/search/tfind.c
  libc-top-half/musl/src/search/tsearch.c
  libc-top-half/musl/src/search/twalk.c
  libc-top-half/musl/src/multibyte/btowc.c
  libc-top-half/musl/src/multibyte/c16rtomb.c
  libc-top-half/musl/src/multibyte/c32rtomb.c
  libc-top-half/musl/src/multibyte/internal.c
  libc-top-half/musl/src/multibyte/mblen.c
  libc-top-half/musl/src/multibyte/mbrlen.c
  libc-top-half/musl/src/multibyte/mbrtoc16.c
  libc-top-half/musl/src/multibyte/mbrtoc32.c
  libc-top-half/musl/src/multibyte/mbrtowc.c
  libc-top-half/musl/src/multibyte/mbsinit.c
  libc-top-half/musl/src/multibyte/mbsnrtowcs.c
  libc-top-half/musl/src/multibyte/mbsrtowcs.c
  libc-top-half/musl/src/multibyte/mbstowcs.c
  libc-top-half/musl/src/multibyte/mbtowc.c
  libc-top-half/musl/src/multibyte/wcrtomb.c
  libc-top-half/musl/src/multibyte/wcsnrtombs.c
  libc-top-half/musl/src/multibyte/wcsrtombs.c
  libc-top-half/musl/src/multibyte/wcstombs.c
  libc-top-half/musl/src/multibyte/wctob.c
  libc-top-half/musl/src/multibyte/wctomb.c
  libc-top-half/musl/src/regex/fnmatch.c
  libc-top-half/musl/src/regex/glob.c
  libc-top-half/musl/src/regex/regcomp.c
  libc-top-half/musl/src/regex/regerror.c
  libc-top-half/musl/src/regex/regexec.c
  libc-top-half/musl/src/regex/tre-mem.c
  libc-top-half/musl/src/prng/drand48.c
  libc-top-half/musl/src/prng/lcong48.c
  libc-top-half/musl/src/prng/lrand48.c
  libc-top-half/musl/src/prng/mrand48.c
  libc-top-half/musl/src/prng/__rand48_step.c
  libc-top-half/musl/src/prng/rand.c
  libc-top-half/musl/src/prng/random.c
  libc-top-half/musl/src/prng/rand_r.c
  libc-top-half/musl/src/prng/__seed48.c
  libc-top-half/musl/src/prng/seed48.c
  libc-top-half/musl/src/prng/srand48.c
  libc-top-half/musl/src/conf/confstr.c
  libc-top-half/musl/src/conf/fpathconf.c
  libc-top-half/musl/src/conf/legacy.c
  libc-top-half/musl/src/conf/pathconf.c
  libc-top-half/musl/src/conf/sysconf.c
  libc-top-half/musl/src/ctype/__ctype_b_loc.c
  libc-top-half/musl/src/ctype/__ctype_get_mb_cur_max.c
  libc-top-half/musl/src/ctype/__ctype_tolower_loc.c
  libc-top-half/musl/src/ctype/__ctype_toupper_loc.c
  libc-top-half/musl/src/ctype/isalnum.c
  libc-top-half/musl/src/ctype/isalpha.c
  libc-top-half/musl/src/ctype/isascii.c
  libc-top-half/musl/src/ctype/isblank.c
  libc-top-half/musl/src/ctype/iscntrl.c
  libc-top-half/musl/src/ctype/isdigit.c
  libc-top-half/musl/src/ctype/isgraph.c
  libc-top-half/musl/src/ctype/islower.c
  libc-top-half/musl/src/ctype/isprint.c
  libc-top-half/musl/src/ctype/ispunct.c
  libc-top-half/musl/src/ctype/isspace.c
  libc-top-half/musl/src/ctype/isupper.c
  libc-top-half/musl/src/ctype/iswalnum.c
  libc-top-half/musl/src/ctype/iswalpha.c
  libc-top-half/musl/src/ctype/iswblank.c
  libc-top-half/musl/src/ctype/iswcntrl.c
  libc-top-half/musl/src/ctype/iswctype.c
  libc-top-half/musl/src/ctype/iswdigit.c
  libc-top-half/musl/src/ctype/iswgraph.c
  libc-top-half/musl/src/ctype/iswlower.c
  libc-top-half/musl/src/ctype/iswprint.c
  libc-top-half/musl/src/ctype/iswpunct.c
  libc-top-half/musl/src/ctype/iswspace.c
  libc-top-half/musl/src/ctype/iswupper.c
  libc-top-half/musl/src/ctype/iswxdigit.c
  libc-top-half/musl/src/ctype/isxdigit.c
  libc-top-half/musl/src/ctype/toascii.c
  libc-top-half/musl/src/ctype/tolower.c
  libc-top-half/musl/src/ctype/toupper.c
  libc-top-half/musl/src/ctype/towctrans.c
  libc-top-half/musl/src/ctype/wcswidth.c
  libc-top-half/musl/src/ctype/wctrans.c
  libc-top-half/musl/src/ctype/wcwidth.c
  libc-top-half/musl/src/math/acos.c
  libc-top-half/musl/src/math/acosf.c
  libc-top-half/musl/src/math/acosh.c
  libc-top-half/musl/src/math/acoshf.c
  libc-top-half/musl/src/math/acoshl.c
  libc-top-half/musl/src/math/acosl.c
  libc-top-half/musl/src/math/asin.c
  libc-top-half/musl/src/math/asinf.c
  libc-top-half/musl/src/math/asinh.c
  libc-top-half/musl/src/math/asinhf.c
  libc-top-half/musl/src/math/asinhl.c
  libc-top-half/musl/src/math/asinl.c
  libc-top-half/musl/src/math/atan2.c
  libc-top-half/musl/src/math/atan2f.c
  libc-top-half/musl/src/math/atan2l.c
  libc-top-half/musl/src/math/atan.c
  libc-top-half/musl/src/math/atanf.c
  libc-top-half/musl/src/math/atanh.c
  libc-top-half/musl/src/math/atanhf.c
  libc-top-half/musl/src/math/atanhl.c
  libc-top-half/musl/src/math/atanl.c
  libc-top-half/musl/src/math/cbrt.c
  libc-top-half/musl/src/math/cbrtf.c
  libc-top-half/musl/src/math/cbrtl.c
  libc-top-half/musl/src/math/ceill.c
  libc-top-half/musl/src/math/copysignl.c
  libc-top-half/musl/src/math/__cos.c
  libc-top-half/musl/src/math/cos.c
  libc-top-half/musl/src/math/__cosdf.c
  libc-top-half/musl/src/math/cosf.c
  libc-top-half/musl/src/math/cosh.c
  libc-top-half/musl/src/math/coshf.c
  libc-top-half/musl/src/math/coshl.c
  libc-top-half/musl/src/math/__cosl.c
  libc-top-half/musl/src/math/cosl.c
  libc-top-half/musl/src/math/erf.c
  libc-top-half/musl/src/math/erff.c
  libc-top-half/musl/src/math/erfl.c
  libc-top-half/musl/src/math/exp10.c
  libc-top-half/musl/src/math/exp10f.c
  libc-top-half/musl/src/math/exp10l.c
  libc-top-half/musl/src/math/exp2.c
  libc-top-half/musl/src/math/exp2f.c
  libc-top-half/musl/src/math/exp2f_data.c
  libc-top-half/musl/src/math/exp2l.c
  libc-top-half/musl/src/math/exp.c
  libc-top-half/musl/src/math/exp_data.c
  libc-top-half/musl/src/math/expf.c
  libc-top-half/musl/src/math/expl.c
  libc-top-half/musl/src/math/expm1.c
  libc-top-half/musl/src/math/expm1f.c
  libc-top-half/musl/src/math/expm1l.c
  libc-top-half/musl/src/math/__expo2.c
  libc-top-half/musl/src/math/__expo2f.c
  libc-top-half/musl/src/math/fabsl.c
  libc-top-half/musl/src/math/fdim.c
  libc-top-half/musl/src/math/fdimf.c
  libc-top-half/musl/src/math/fdiml.c
  libc-top-half/musl/src/math/finite.c
  libc-top-half/musl/src/math/finitef.c
  libc-top-half/musl/src/math/floorl.c
  libc-top-half/musl/src/math/fma.c
  libc-top-half/musl/src/math/fmaf.c
  libc-top-half/musl/src/math/fmal.c
  libc-top-half/musl/src/math/fmaxl.c
  libc-top-half/musl/src/math/fminl.c
  libc-top-half/musl/src/math/fmod.c
  libc-top-half/musl/src/math/fmodf.c
  libc-top-half/musl/src/math/fmodl.c
  libc-top-half/musl/src/math/frexp.c
  libc-top-half/musl/src/math/frexpf.c
  libc-top-half/musl/src/math/frexpl.c
  libc-top-half/musl/src/math/hypot.c
  libc-top-half/musl/src/math/hypotf.c
  libc-top-half/musl/src/math/hypotl.c
  libc-top-half/musl/src/math/ilogb.c
  libc-top-half/musl/src/math/ilogbf.c
  libc-top-half/musl/src/math/ilogbl.c
  libc-top-half/musl/src/math/__invtrigl.c
  libc-top-half/musl/src/math/j0.c
  libc-top-half/musl/src/math/j0f.c
  libc-top-half/musl/src/math/j1.c
  libc-top-half/musl/src/math/j1f.c
  libc-top-half/musl/src/math/jn.c
  libc-top-half/musl/src/math/jnf.c
  libc-top-half/musl/src/math/ldexp.c
  libc-top-half/musl/src/math/ldexpf.c
  libc-top-half/musl/src/math/ldexpl.c
  libc-top-half/musl/src/math/lgamma.c
  libc-top-half/musl/src/math/lgammaf.c
  libc-top-half/musl/src/math/lgammaf_r.c
  libc-top-half/musl/src/math/lgammal.c
  libc-top-half/musl/src/math/lgamma_r.c
  libc-top-half/musl/src/math/llrint.c
  libc-top-half/musl/src/math/llrintf.c
  libc-top-half/musl/src/math/llrintl.c
  libc-top-half/musl/src/math/llround.c
  libc-top-half/musl/src/math/llroundf.c
  libc-top-half/musl/src/math/llroundl.c
  libc-top-half/musl/src/math/log10.c
  libc-top-half/musl/src/math/log10f.c
  libc-top-half/musl/src/math/log10l.c
  libc-top-half/musl/src/math/log1p.c
  libc-top-half/musl/src/math/log1pf.c
  libc-top-half/musl/src/math/log1pl.c
  libc-top-half/musl/src/math/log2.c
  libc-top-half/musl/src/math/log2_data.c
  libc-top-half/musl/src/math/log2f.c
  libc-top-half/musl/src/math/log2f_data.c
  libc-top-half/musl/src/math/log2l.c
  libc-top-half/musl/src/math/logb.c
  libc-top-half/musl/src/math/logbf.c
  libc-top-half/musl/src/math/logbl.c
  libc-top-half/musl/src/math/log.c
  libc-top-half/musl/src/math/log_data.c
  libc-top-half/musl/src/math/logf.c
  libc-top-half/musl/src/math/logf_data.c
  libc-top-half/musl/src/math/logl.c
  libc-top-half/musl/src/math/lrint.c
  libc-top-half/musl/src/math/lrintf.c
  libc-top-half/musl/src/math/lrintl.c
  libc-top-half/musl/src/math/lround.c
  libc-top-half/musl/src/math/lroundf.c
  libc-top-half/musl/src/math/lroundl.c
  libc-top-half/musl/src/math/__math_divzero.c
  libc-top-half/musl/src/math/__math_divzerof.c
  libc-top-half/musl/src/math/__math_invalid.c
  libc-top-half/musl/src/math/__math_invalidf.c
  libc-top-half/musl/src/math/__math_oflow.c
  libc-top-half/musl/src/math/__math_oflowf.c
  libc-top-half/musl/src/math/__math_uflow.c
  libc-top-half/musl/src/math/__math_uflowf.c
  libc-top-half/musl/src/math/__math_xflow.c
  libc-top-half/musl/src/math/__math_xflowf.c
  libc-top-half/musl/src/math/modf.c
  libc-top-half/musl/src/math/modff.c
  libc-top-half/musl/src/math/modfl.c
  libc-top-half/musl/src/math/nan.c
  libc-top-half/musl/src/math/nanf.c
  libc-top-half/musl/src/math/nanl.c
  libc-top-half/musl/src/math/nearbyintl.c
  libc-top-half/musl/src/math/nextafter.c
  libc-top-half/musl/src/math/nextafterf.c
  libc-top-half/musl/src/math/nextafterl.c
  libc-top-half/musl/src/math/nexttoward.c
  libc-top-half/musl/src/math/nexttowardf.c
  libc-top-half/musl/src/math/nexttowardl.c
  libc-top-half/musl/src/math/__polevll.c
  libc-top-half/musl/src/math/pow.c
  libc-top-half/musl/src/math/pow_data.c
  libc-top-half/musl/src/math/powf.c
  libc-top-half/musl/src/math/powf_data.c
  libc-top-half/musl/src/math/powl.c
  libc-top-half/musl/src/math/remainder.c
  libc-top-half/musl/src/math/remainderf.c
  libc-top-half/musl/src/math/remainderl.c
  libc-top-half/musl/src/math/__rem_pio2.c
  libc-top-half/musl/src/math/__rem_pio2f.c
  libc-top-half/musl/src/math/__rem_pio2_large.c
  libc-top-half/musl/src/math/__rem_pio2l.c
  libc-top-half/musl/src/math/remquo.c
  libc-top-half/musl/src/math/remquof.c
  libc-top-half/musl/src/math/remquol.c
  libc-top-half/musl/src/math/rintl.c
  libc-top-half/musl/src/math/round.c
  libc-top-half/musl/src/math/roundf.c
  libc-top-half/musl/src/math/roundl.c
  libc-top-half/musl/src/math/scalb.c
  libc-top-half/musl/src/math/scalbf.c
  libc-top-half/musl/src/math/scalbln.c
  libc-top-half/musl/src/math/scalblnf.c
  libc-top-half/musl/src/math/scalblnl.c
  libc-top-half/musl/src/math/scalbn.c
  libc-top-half/musl/src/math/scalbnf.c
  libc-top-half/musl/src/math/scalbnl.c
  libc-top-half/musl/src/math/signgam.c
  libc-top-half/musl/src/math/significand.c
  libc-top-half/musl/src/math/significandf.c
  libc-top-half/musl/src/math/__sin.c
  libc-top-half/musl/src/math/sin.c
  libc-top-half/musl/src/math/sincos.c
  libc-top-half/musl/src/math/sincosf.c
  libc-top-half/musl/src/math/sincosl.c
  libc-top-half/musl/src/math/__sindf.c
  libc-top-half/musl/src/math/sinf.c
  libc-top-half/musl/src/math/sinh.c
  libc-top-half/musl/src/math/sinhf.c
  libc-top-half/musl/src/math/sinhl.c
  libc-top-half/musl/src/math/__sinl.c
  libc-top-half/musl/src/math/sinl.c
  libc-top-half/musl/src/math/sqrtl.c
  libc-top-half/musl/src/math/__tan.c
  libc-top-half/musl/src/math/tan.c
  libc-top-half/musl/src/math/__tandf.c
  libc-top-half/musl/src/math/tanf.c
  libc-top-half/musl/src/math/tanh.c
  libc-top-half/musl/src/math/tanhf.c
  libc-top-half/musl/src/math/tanhl.c
  libc-top-half/musl/src/math/__tanl.c
  libc-top-half/musl/src/math/tanl.c
  libc-top-half/musl/src/math/tgamma.c
  libc-top-half/musl/src/math/tgammaf.c
  libc-top-half/musl/src/math/tgammal.c
  libc-top-half/musl/src/math/truncl.c
  libc-top-half/musl/src/complex/cabs.c
  libc-top-half/musl/src/complex/cabsf.c
  libc-top-half/musl/src/complex/cabsl.c
  libc-top-half/musl/src/complex/cacos.c
  libc-top-half/musl/src/complex/cacosf.c
  libc-top-half/musl/src/complex/cacosh.c
  libc-top-half/musl/src/complex/cacoshf.c
  libc-top-half/musl/src/complex/cacoshl.c
  libc-top-half/musl/src/complex/cacosl.c
  libc-top-half/musl/src/complex/carg.c
  libc-top-half/musl/src/complex/cargf.c
  libc-top-half/musl/src/complex/cargl.c
  libc-top-half/musl/src/complex/casin.c
  libc-top-half/musl/src/complex/casinf.c
  libc-top-half/musl/src/complex/casinh.c
  libc-top-half/musl/src/complex/casinhf.c
  libc-top-half/musl/src/complex/casinhl.c
  libc-top-half/musl/src/complex/casinl.c
  libc-top-half/musl/src/complex/catan.c
  libc-top-half/musl/src/complex/catanf.c
  libc-top-half/musl/src/complex/catanh.c
  libc-top-half/musl/src/complex/catanhf.c
  libc-top-half/musl/src/complex/catanhl.c
  libc-top-half/musl/src/complex/catanl.c
  libc-top-half/musl/src/complex/ccos.c
  libc-top-half/musl/src/complex/ccosf.c
  libc-top-half/musl/src/complex/ccosh.c
  libc-top-half/musl/src/complex/ccoshf.c
  libc-top-half/musl/src/complex/ccoshl.c
  libc-top-half/musl/src/complex/ccosl.c
  libc-top-half/musl/src/complex/__cexp.c
  libc-top-half/musl/src/complex/cexp.c
  libc-top-half/musl/src/complex/__cexpf.c
  libc-top-half/musl/src/complex/cexpf.c
  libc-top-half/musl/src/complex/cexpl.c
  libc-top-half/musl/src/complex/cimagl.c
  libc-top-half/musl/src/complex/clog.c
  libc-top-half/musl/src/complex/clogf.c
  libc-top-half/musl/src/complex/clogl.c
  libc-top-half/musl/src/complex/conj.c
  libc-top-half/musl/src/complex/conjf.c
  libc-top-half/musl/src/complex/conjl.c
  libc-top-half/musl/src/complex/cpow.c
  libc-top-half/musl/src/complex/cpowf.c
  libc-top-half/musl/src/complex/cpowl.c
  libc-top-half/musl/src/complex/cproj.c
  libc-top-half/musl/src/complex/cprojf.c
  libc-top-half/musl/src/complex/cprojl.c
  libc-top-half/musl/src/complex/creall.c
  libc-top-half/musl/src/complex/csin.c
  libc-top-half/musl/src/complex/csinf.c
  libc-top-half/musl/src/complex/csinh.c
  libc-top-half/musl/src/complex/csinhf.c
  libc-top-half/musl/src/complex/csinhl.c
  libc-top-half/musl/src/complex/csinl.c
  libc-top-half/musl/src/complex/csqrt.c
  libc-top-half/musl/src/complex/csqrtf.c
  libc-top-half/musl/src/complex/csqrtl.c
  libc-top-half/musl/src/complex/ctan.c
  libc-top-half/musl/src/complex/ctanf.c
  libc-top-half/musl/src/complex/ctanh.c
  libc-top-half/musl/src/complex/ctanhf.c
  libc-top-half/musl/src/complex/ctanhl.c
  libc-top-half/musl/src/complex/ctanl.c
  libc-top-half/musl/src/crypt/crypt_blowfish.c
  libc-top-half/musl/src/crypt/crypt.c
  libc-top-half/musl/src/crypt/crypt_des.c
  libc-top-half/musl/src/crypt/crypt_md5.c
  libc-top-half/musl/src/crypt/crypt_r.c
  libc-top-half/musl/src/crypt/crypt_sha256.c
  libc-top-half/musl/src/crypt/crypt_sha512.c
  libc-top-half/musl/src/crypt/encrypt.c
  libc-top-half/sources/arc4random.c)
target_include_directories(libc-top-half PRIVATE
  libc-top-half/musl/src/include
  libc-top-half/musl/src/internal
  libc-top-half/musl/arch/wasm32
  libc-top-half/musl/arch/generic
  libc-top-half/headers/private)
target_compile_options(libc-top-half PRIVATE
  -Wno-parentheses
  -Wno-shift-op-parentheses
  -Wno-bitwise-op-parentheses
  -Wno-logical-op-parentheses
  -Wno-string-plus-int
  -Wno-dangling-else
  -Wno-unknown-pragmas)
add_dependencies(libc-top-half sysroot-headers)

target_link_libraries(c PRIVATE
  libc-bottom-half
  libc-top-half
  dlmalloc)

foreach(stub m rt pthread crypt util xnet resolve dl)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/sysroot/lib/lib${stub}.a
    COMMAND ${CMAKE_AR} crs ${CMAKE_BINARY_DIR}/sysroot/lib/lib${stub}.a)
  add_custom_target(${stub} ALL
    DEPENDS ${CMAKE_BINARY_DIR}/sysroot/lib/lib${stub}.a)
endforeach()

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/sysroot/lib/libc.imports
  COMMAND
    ${Python_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen-imports.py
    --nm ${CMAKE_NM}
    --output ${CMAKE_BINARY_DIR}/sysroot/lib/libc.imports
    $<TARGET_FILE:c>
    ${CMAKE_BINARY_DIR}/sysroot/lib/crt1.o
  DEPENDS
    ${PROJECT_SOURCE_DIR}/tools/gen-imports.py)
add_custom_target(libc.imports ALL
  DEPENDS ${CMAKE_BINARY_DIR}/sysroot/lib/libc.imports)
add_dependencies(libc.imports c crt1.o)

add_test(NAME check-metadata
  COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${PROJECT_SOURCE_DIR}/expected/wasm32-wasi/predefined-macros.txt ${CMAKE_BINARY_DIR}/sysroot/share/predefined-macros.txt)
add_test(NAME check-headers
  COMMAND ${CMAKE_C_COMPILER} -target ${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_BINARY_DIR}/sysroot -fsyntax-only "-Wno#warnings" ${CMAKE_BINARY_DIR}/sysroot/share/include-all.c)
