# Disable some warnings in this third-party code.
add_compile_options(
  -Wno-bitwise-op-parentheses
  -Wno-dangling-else
  -Wno-ignored-attributes
  -Wno-ignored-pragmas
  -Wno-logical-op-parentheses
  -Wno-missing-braces
  -Wno-parentheses
  -Wno-shift-op-parentheses
  -Wno-sign-compare
  -Wno-string-plus-int
  -Wno-unknown-pragmas
  -Wno-unused-but-set-variable
  -Wno-unused-function
  -Wno-unused-parameter
  -Wno-unused-variable
)

# Dummy interface library which helps configure headers/options for internal
# libraries below.
add_library(musl-top-half-interface INTERFACE)
target_include_directories(musl-top-half-interface INTERFACE
  musl/src/include
  musl/src/internal
  musl/arch/wasm32
  musl/arch/generic
  headers/private)
add_dependencies(musl-top-half-interface sysroot_inc)

# =============================================================================
# sysroot headers from the top-half
#

# Generates `bits/alltypes.h` from the input sed script.
find_program(SED sed REQUIRED)
add_custom_command(
  OUTPUT ${SYSROOT_INC}/bits/alltypes.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_INC}/bits
  COMMAND
    ${SED} -f musl/tools/mkalltypes.sed
      musl/arch/wasm32/bits/alltypes.h.in
      musl/include/alltypes.h.in
      > ${SYSROOT_INC}/bits/alltypes.h
  WORKING_DIRECTORY
    ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS
    musl/tools/mkalltypes.sed
    musl/arch/wasm32/bits/alltypes.h.in
    musl/include/alltypes.h.in
)
add_custom_target(sysroot-bits-alltypes DEPENDS ${SYSROOT_INC}/bits/alltypes.h)
add_dependencies(sysroot_inc sysroot-bits-alltypes)

file(GLOB_RECURSE globbed_headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  # Copy in the bulk of musl's public header files.
  musl/include/*.h
  # Copy in the musl's "bits" header files.
  musl/arch/generic/bits/*.h
  musl/arch/wasm32/bits/*.h
)

list(REMOVE_ITEM globbed_headers
  # Use the compiler's version of these headers.
  musl/include/stdarg.h
  musl/include/stddef.h

  # Use the WASI errno definitions.
  musl/arch/generic/bits/errno.h

  # Remove headers that aren't supported yet or that aren't relevant for WASI.
  musl/arch/generic/bits/dirent.h
  musl/arch/generic/bits/fcntl.h
  musl/arch/generic/bits/ioctl.h
  musl/arch/generic/bits/ipc.h
  musl/arch/generic/bits/kd.h
  musl/arch/generic/bits/limits.h
  musl/arch/generic/bits/link.h
  musl/arch/generic/bits/msg.h
  musl/arch/generic/bits/ptrace.h
  musl/arch/generic/bits/sem.h
  musl/arch/generic/bits/shm.h
  musl/arch/generic/bits/soundcard.h
  musl/arch/generic/bits/statfs.h
  musl/arch/generic/bits/termios.h
  musl/arch/generic/bits/vt.h
  musl/include/aio.h
  musl/include/elf.h
  musl/include/grp.h
  musl/include/lastlog.h
  musl/include/libintl.h
  musl/include/link.h
  musl/include/mntent.h
  musl/include/net/ethernet.h
  musl/include/net/if.h
  musl/include/net/if_arp.h
  musl/include/net/route.h
  musl/include/netinet/ether.h
  musl/include/netinet/if_ether.h
  musl/include/paths.h
  musl/include/pty.h
  musl/include/pwd.h
  musl/include/pwd.h
  musl/include/resolv.h
  musl/include/scsi/scsi.h
  musl/include/scsi/scsi_ioctl.h
  musl/include/scsi/sg.h
  musl/include/shadow.h
  musl/include/spawn.h
  musl/include/sys/acct.h
  musl/include/sys/auxv.h
  musl/include/sys/cachectl.h
  musl/include/sys/epoll.h
  musl/include/sys/fanotify.h
  musl/include/sys/fsuid.h
  musl/include/sys/inotify.h
  musl/include/sys/io.h
  musl/include/sys/ipc.h
  musl/include/sys/kd.h
  musl/include/sys/klog.h
  musl/include/sys/membarrier.h
  musl/include/sys/mount.h
  musl/include/sys/msg.h
  musl/include/sys/mtio.h
  musl/include/sys/personality.h
  musl/include/sys/prctl.h
  musl/include/sys/procfs.h
  musl/include/sys/ptrace.h
  musl/include/sys/quota.h
  musl/include/sys/reboot.h
  musl/include/sys/sem.h
  musl/include/sys/sendfile.h
  musl/include/sys/shm.h
  musl/include/sys/signalfd.h
  musl/include/sys/soundcard.h
  musl/include/sys/statfs.h
  musl/include/sys/swap.h
  musl/include/sys/syslog.h
  musl/include/sys/sysmacros.h
  musl/include/sys/termios.h
  musl/include/sys/timerfd.h
  musl/include/sys/ucontext.h
  musl/include/sys/user.h
  musl/include/sys/vfs.h
  musl/include/sys/vt.h
  musl/include/sys/wait.h
  musl/include/sys/xattr.h
  musl/include/syslog.h
  musl/include/termios.h
  musl/include/ucontext.h
  musl/include/ulimit.h
  musl/include/utmp.h
  musl/include/utmpx.h
  musl/include/wait.h
  musl/include/wordexp.h
)

# Exclude `netdb.h` from all of the p1 targets and webp2.
if(WASI STREQUAL "p1" OR WEB STREQUAL "p2")
  list(REMOVE_ITEM globbed_headers
    musl/include/netdb.h
  )
endif()

foreach(header ${globbed_headers})
  if(${header} MATCHES "^musl/include/")
    string(REPLACE "musl/include/" "" dst ${header})
  elseif(${header} MATCHES "^musl/arch/generic/bits/")
    string(REPLACE "musl/arch/generic/" "" dst ${header})
  elseif(${header} MATCHES "^musl/arch/wasm32/bits/")
    string(REPLACE "musl/arch/wasm32/" "" dst ${header})
  else()
    message(FATAL_ERROR "Unhandled header file path: ${header}")
  endif()
  add_sysroot_header(${header} ${dst})
endforeach()

# =============================================================================
# Object libraries for in-tree source files
#

set(top_half_sources
  musl/src/dirent/alphasort.c
  musl/src/dirent/versionsort.c
  musl/src/env/__init_tls.c
  musl/src/env/__stack_chk_fail.c
  musl/src/env/clearenv.c
  musl/src/env/getenv.c
  musl/src/env/putenv.c
  musl/src/env/setenv.c
  musl/src/env/unsetenv.c
  musl/src/errno/strerror.c
  musl/src/exit/assert.c
  musl/src/exit/at_quick_exit.c
  musl/src/exit/atexit.c
  musl/src/exit/exit.c
  musl/src/exit/quick_exit.c
  musl/src/fcntl/creat.c
  musl/src/fenv/fegetexceptflag.c
  musl/src/fenv/feholdexcept.c
  musl/src/fenv/fenv.c
  musl/src/fenv/fesetexceptflag.c
  musl/src/fenv/fesetround.c
  musl/src/fenv/feupdateenv.c
  musl/src/internal/defsysinfo.c
  musl/src/internal/intscan.c
  musl/src/internal/libc.c
  musl/src/internal/shgetc.c
  musl/src/legacy/getpagesize.c
  musl/src/misc/a64l.c
  musl/src/misc/basename.c
  musl/src/misc/dirname.c
  musl/src/misc/ffs.c
  musl/src/misc/ffsl.c
  musl/src/misc/ffsll.c
  musl/src/misc/fmtmsg.c
  musl/src/misc/getdomainname.c
  musl/src/misc/gethostid.c
  musl/src/misc/getopt.c
  musl/src/misc/getopt_long.c
  musl/src/misc/getsubopt.c
  musl/src/misc/nftw.c
  musl/src/misc/realpath.c
  musl/src/misc/uname.c
  musl/src/network/htonl.c
  musl/src/network/htons.c
  musl/src/network/in6addr_any.c
  musl/src/network/in6addr_loopback.c
  musl/src/network/inet_addr.c
  musl/src/network/inet_aton.c
  musl/src/network/inet_legacy.c
  musl/src/network/inet_ntoa.c
  musl/src/network/inet_ntop.c
  musl/src/network/inet_pton.c
  musl/src/network/ntohl.c
  musl/src/network/ntohs.c
  musl/src/stat/futimesat.c
  musl/src/thread/common/__lock.c
  musl/src/thread/common/__wait.c
  musl/src/thread/common/__timedwait.c
  musl/src/thread/common/default_attr.c
  musl/src/thread/common/pthread_attr_destroy.c
  musl/src/thread/common/pthread_attr_get.c
  musl/src/thread/common/pthread_attr_init.c
  musl/src/thread/common/pthread_attr_setdetachstate.c
  musl/src/thread/common/pthread_attr_setguardsize.c
  musl/src/thread/common/pthread_attr_setschedparam.c
  musl/src/thread/common/pthread_attr_setstack.c
  musl/src/thread/common/pthread_attr_setstacksize.c
  musl/src/thread/common/pthread_barrierattr_destroy.c
  musl/src/thread/common/pthread_barrierattr_init.c
  musl/src/thread/common/pthread_barrierattr_setpshared.c
  musl/src/thread/common/pthread_barrier_wait.c
  musl/src/thread/common/pthread_barrier_destroy.c
  musl/src/thread/common/pthread_barrier_init.c
  musl/src/thread/common/pthread_cancel.c
  musl/src/thread/common/pthread_cleanup_push.c
  musl/src/thread/common/pthread_condattr_destroy.c
  musl/src/thread/common/pthread_condattr_init.c
  musl/src/thread/common/pthread_condattr_setclock.c
  musl/src/thread/common/pthread_condattr_setpshared.c
  musl/src/thread/common/pthread_equal.c
  musl/src/thread/common/pthread_getattr_np.c
  musl/src/thread/common/pthread_getspecific.c
  musl/src/thread/common/pthread_key_create.c
  musl/src/thread/common/pthread_mutex_destroy.c
  musl/src/thread/common/pthread_mutex_init.c
  musl/src/thread/common/pthread_mutexattr_destroy.c
  musl/src/thread/common/pthread_mutexattr_init.c
  musl/src/thread/common/pthread_mutexattr_setprotocol.c
  musl/src/thread/common/pthread_mutexattr_setpshared.c
  musl/src/thread/common/pthread_mutexattr_setrobust.c
  musl/src/thread/common/pthread_mutexattr_settype.c
  musl/src/thread/common/pthread_rwlock_destroy.c
  musl/src/thread/common/pthread_rwlock_init.c
  musl/src/thread/common/pthread_rwlockattr_destroy.c
  musl/src/thread/common/pthread_rwlockattr_init.c
  musl/src/thread/common/pthread_rwlockattr_setpshared.c
  musl/src/thread/common/pthread_self.c
  musl/src/thread/common/pthread_setcancelstate.c
  musl/src/thread/common/pthread_setcanceltype.c
  musl/src/thread/common/pthread_setspecific.c
  musl/src/thread/common/pthread_spin_destroy.c
  musl/src/thread/common/pthread_spin_init.c
  musl/src/thread/common/pthread_testcancel.c
  musl/src/thread/common/pthread_cond_broadcast.c
  musl/src/thread/common/pthread_cond_destroy.c
  musl/src/thread/common/pthread_cond_init.c
  musl/src/thread/common/pthread_cond_signal.c
  musl/src/thread/common/pthread_cond_timedwait.c
  musl/src/thread/common/pthread_cond_wait.c
  musl/src/thread/common/pthread_mutex_consistent.c
  musl/src/thread/common/pthread_mutex_getprioceiling.c
  musl/src/thread/common/pthread_mutex_setprioceiling.c
  musl/src/thread/common/pthread_mutex_lock.c
  musl/src/thread/common/pthread_mutex_timedlock.c
  musl/src/thread/common/pthread_mutex_trylock.c
  musl/src/thread/common/pthread_mutex_unlock.c
  musl/src/thread/common/pthread_once.c
  musl/src/thread/common/pthread_rwlock_rdlock.c
  musl/src/thread/common/pthread_rwlock_timedrdlock.c
  musl/src/thread/common/pthread_rwlock_timedwrlock.c
  musl/src/thread/common/pthread_rwlock_tryrdlock.c
  musl/src/thread/common/pthread_rwlock_trywrlock.c
  musl/src/thread/common/pthread_rwlock_unlock.c
  musl/src/thread/common/pthread_rwlock_wrlock.c
  musl/src/thread/common/pthread_spin_lock.c
  musl/src/thread/common/pthread_spin_trylock.c
  musl/src/thread/common/pthread_spin_unlock.c
  musl/src/thread/common/pthread_setconcurrency.c
  musl/src/thread/common/mtx_destroy.c
  musl/src/thread/common/mtx_init.c
  musl/src/thread/common/mtx_lock.c
  musl/src/thread/common/mtx_timedlock.c
  musl/src/thread/common/mtx_trylock.c
  musl/src/thread/common/thrd_sleep.c
  musl/src/thread/common/thrd_join.c
  musl/src/thread/common/thrd_yield.c
  musl/src/thread/common/sem_destroy.c
  musl/src/thread/common/sem_getvalue.c
  musl/src/thread/common/sem_init.c
  musl/src/thread/common/sem_post.c
  musl/src/thread/common/sem_timedwait.c
  musl/src/thread/common/sem_trywait.c
  musl/src/thread/common/sem_wait.c
  musl/src/thread/common/sem_open.c
  musl/src/thread/common/lock_ptc.c
  musl/src/thread/common/pthread_getcpuclockid.c
  musl/src/thread/common/pthread_getschedparam.c
  musl/src/thread/common/pthread_setschedparam.c
  musl/src/thread/common/pthread_setschedprio.c
  musl/src/time/__month_to_secs.c
  musl/src/time/__secs_to_tm.c
  musl/src/time/__tm_to_secs.c
  musl/src/time/__tz.c
  musl/src/time/__utc.c
  musl/src/time/__year_to_secs.c
  musl/src/time/asctime.c
  musl/src/time/asctime_r.c
  musl/src/time/ctime.c
  musl/src/time/ctime_r.c
  musl/src/time/difftime.c
  musl/src/time/ftime.c
  musl/src/time/getdate.c
  musl/src/time/gmtime.c
  musl/src/time/gmtime_r.c
  musl/src/time/localtime.c
  musl/src/time/localtime_r.c
  musl/src/time/mktime.c
  musl/src/time/strftime.c
  musl/src/time/strptime.c
  musl/src/time/timegm.c
  musl/src/time/timespec_get.c
  musl/src/time/wcsftime.c
  musl/src/unistd/gethostname.c
  musl/src/unistd/posix_close.c
  sources/arc4random.c
)

set(MUSL_PRINTSCAN_SOURCES
  musl/src/internal/floatscan.c
  musl/src/stdio/vfprintf.c
  musl/src/stdio/vfscanf.c
  musl/src/stdio/vfwprintf.c
  musl/src/stdlib/strtod.c
  musl/src/stdlib/wcstod.c)

set(MUSL_BULK_MEMORY_SOURCES
  musl/src/string/memcpy.c
  musl/src/string/memmove.c
  musl/src/string/memset.c)

# Note that cmake generally recommends against globbing sources, but there's so
# many here and this doesn't change enough that it feels worth it...
#
# THis also pulls in too many sources, so more are removed below.
file(GLOB globbed_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  musl/src/complex/*.c
  musl/src/conf/*.c
  musl/src/crypt/*.c
  musl/src/ctype/*.c
  musl/src/locale/*.c
  musl/src/math/*.c
  musl/src/multibyte/*.c
  musl/src/prng/*.c
  musl/src/regex/*.c
  musl/src/search/*.c
  musl/src/stdio/*.c
  musl/src/stdlib/*.c
  musl/src/string/*.c
)
list(REMOVE_ITEM globbed_sources
  # handled below during printscan/bulk-memory library handling
  ${MUSL_PRINTSCAN_SOURCES}
  ${MUSL_BULK_MEMORY_SOURCES}
  # not needed and/or don't compile on wasi
  musl/src/complex/cimag.c
  musl/src/complex/cimagf.c
  musl/src/complex/cimagl.c
  musl/src/complex/creal.c
  musl/src/complex/crealf.c
  musl/src/complex/creall.c
  musl/src/locale/bind_textdomain_codeset.c
  musl/src/locale/dcngettext.c
  musl/src/locale/textdomain.c
  musl/src/math/__fpclassify.c
  musl/src/math/__fpclassifyf.c
  musl/src/math/__fpclassifyl.c
  musl/src/math/__signbit.c
  musl/src/math/__signbitf.c
  musl/src/math/__signbitl.c
  musl/src/math/ceil.c
  musl/src/math/ceilf.c
  musl/src/math/copysign.c
  musl/src/math/copysignf.c
  musl/src/math/fabs.c
  musl/src/math/fabsf.c
  musl/src/math/floor.c
  musl/src/math/floorf.c
  musl/src/math/fmax.c
  musl/src/math/fmaxf.c
  musl/src/math/fmin.c
  musl/src/math/fminf.c
  musl/src/math/nearbyint.c
  musl/src/math/nearbyintf.c
  musl/src/math/rint.c
  musl/src/math/rintf.c
  musl/src/math/sqrt.c
  musl/src/math/sqrtf.c
  musl/src/math/trunc.c
  musl/src/math/truncf.c
  musl/src/stdio/gets.c
  musl/src/stdio/pclose.c
  musl/src/stdio/popen.c
  musl/src/stdio/remove.c
  musl/src/stdio/rename.c
  musl/src/stdio/tempnam.c
  musl/src/stdio/tmpfile.c
  musl/src/stdio/tmpnam.c
  musl/src/string/strsignal.c
)

list(APPEND top_half_sources ${globbed_sources})

if (WASI STREQUAL "p2" OR WASI STREQUAL "p3")
  list(APPEND top_half_sources
    musl/src/network/gai_strerror.c
  )
endif()

if (TARGET_TRIPLE MATCHES "wasip1-threads")
  # pthreads functions needed for actual thread support
  list(APPEND top_half_sources
    musl/src/thread/wasi-threads/futex.c
    musl/src/thread/wasi-threads/pthread_create.c
    musl/src/thread/wasi-threads/pthread_detach.c
    musl/src/thread/wasi-threads/pthread_join.c
    musl/src/thread/wasi-threads/wasi_thread_start.s
    musl/src/thread/wasi-threads/__wasilibc_busywait.c
  )
elseif(ENABLE_COOP_THREADS)
  # cooperative threads implementation for WASI Preview 3
  list(APPEND top_half_sources
    musl/src/internal/hashmap.c
    musl/src/thread/coop-threads/futex.c
    musl/src/thread/coop-threads/__wasi_coop_thread_start.s
    musl/src/thread/coop-threads/pthread_create.c
    musl/src/thread/coop-threads/pthread_detach.c
    musl/src/thread/coop-threads/pthread_getname_np.c
    musl/src/thread/coop-threads/pthread_join.c
    musl/src/thread/coop-threads/pthread_setname_np.c
  )
else()
  # pthreads stubs for single-threaded environment
  list(APPEND top_half_sources
    musl/src/thread/single-threaded/futex.c
    musl/src/thread/single-threaded/pthread_create.c
    musl/src/thread/single-threaded/pthread_detach.c
    musl/src/thread/single-threaded/pthread_join.c
  )
endif()

add_object_library(top-half ${top_half_sources})
foreach(obj top-half-shared top-half-static)
  target_link_libraries(${obj} PUBLIC musl-top-half-interface)
endforeach()

if (LTO)
  set_source_files_properties(musl/src/exit/atexit.c
     PROPERTIES COMPILE_OPTIONS -fno-lto)
endif()

# =============================================================================
# printscan library handling
#
# There are three copies of the printscan-related functions built:
#
# 1. `printscan_default` - long double support is disabled but float support
#    is enabled.
# 2. `printscan_nofloat` - floats are entirely disabled.
# 3. `printscan_longdouble` - long double + floats enabled
#
# These are the included in various final artifacts in various ways.

add_internal_object_library(printscan-default ${MUSL_PRINTSCAN_SOURCES})
add_internal_object_library(printscan-nofloat ${MUSL_PRINTSCAN_SOURCES})
add_internal_object_library(printscan-longdouble ${MUSL_PRINTSCAN_SOURCES})

foreach(obj printscan-default-shared printscan-default-static)
  target_compile_definitions(${obj} PRIVATE
    __wasilibc_printscan_no_long_double
    __wasilibc_printscan_full_support_option="add -lc-printscan-long-double to the link command")
endforeach()

foreach(obj printscan-nofloat-shared printscan-nofloat-static)
  target_compile_definitions(${obj} PRIVATE
    __wasilibc_printscan_no_floating_point
    __wasilibc_printscan_floating_point_support_option="remove -lc-printscan-no-floating-point from the link command")
endforeach()

# Add explicit `*.a` files to the sysroot which can be manually selected by
# consumers via `-l`.
add_library(c-printscan-long-double STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:printscan-longdouble-static>)
add_library(c-printscan-no-floating-point STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:printscan-nofloat-static>)
sysroot_lib(c-printscan-long-double libc-printscan-long-double.a)
sysroot_lib(c-printscan-no-floating-point libc-printscan-no-floating-point.a)

# =============================================================================
# bulk memory handling
#
# TODO: apply -mbulk-memory globally, once
# https://github.com/llvm/llvm-project/issues/52618 is resolved
add_internal_object_library(bulk-memory ${MUSL_BULK_MEMORY_SOURCES})

foreach(obj bulk-memory-shared bulk-memory-static)
  target_compile_definitions(${obj} PRIVATE -DBULK_MEMORY_THRESHOLD=${BULK_MEMORY_THRESHOLD})
  target_compile_options(${obj} PRIVATE -mbulk-memory)
endforeach()

# =============================================================================
# libsetjmp.{a,so}
#
add_internal_library_pair(setjmp musl/src/setjmp/wasm32/rt.c)

if (LTO)
  set_source_files_properties(musl/src/setjmp/wasm32/rt.c
     PROPERTIES COMPILE_OPTIONS -fno-lto)
endif()

foreach(obj setjmp setjmp-static)
  target_compile_options(${obj} PRIVATE -mllvm -wasm-enable-sjlj)
  target_link_options(${obj} PRIVATE
    -Wl,--allow-undefined-file=${CMAKE_SOURCE_DIR}/linker-provided-symbols.txt)
endforeach()

if (SETJMP)
  sysroot_lib(setjmp libsetjmp.so)
  sysroot_lib(setjmp-static libsetjmp.a)
endif()

# =============================================================================
# libdl.{a,so}
#
add_internal_library_pair(dl musl/src/misc/dl.c)

sysroot_lib(dl libdl.so)
sysroot_lib(dl-static libdl.a)
