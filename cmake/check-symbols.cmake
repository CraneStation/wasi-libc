set(SYSROOT_SHARE ${SYSROOT}/share/${TARGET_TRIPLE})
set(undefined_symbols ${SYSROOT_SHARE}/undefined-symbols.txt)
set(defined_symbols ${SYSROOT_SHARE}/defined-symbols.txt)
set(include_all ${SYSROOT_SHARE}/include-all.c)

add_custom_command(
  OUTPUT
    ${defined_symbols}
    ${undefined_symbols}
    ${include_all}
  COMMAND
    ${CMAKE_COMMAND}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_C_FLAGS="$<TARGET_PROPERTY:c,COMPILE_OPTIONS>"
      -DSYSROOT_LIB=${SYSROOT_LIB}
      -DSYSROOT_INC=${SYSROOT_INC}
      -DCMAKE_NM=${CMAKE_NM}
      -DOUT_DIR=${SYSROOT_SHARE}
      -P ${CMAKE_CURRENT_LIST_DIR}/scripts/run-check-symbols.cmake
  DEPENDS
    sysroot
    ${CMAKE_CURRENT_LIST_DIR}/scripts/run-check-symbols.cmake
)

if(WASI STREQUAL "p1")
  if(TARGET_TRIPLE MATCHES "threads")
    set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-wasip1-threads")
  else()
    set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-wasip1")
  endif()
elseif(WASI STREQUAL "p2")
  set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-wasip2")
elseif(WASI STREQUAL "p3")
  if(ENABLE_COOP_THREADS)
    set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-wasip3-coop")
  else()
    set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-wasip3")
  endif()
elseif(WEB STREQUAL "p2")
  set(expected "${CMAKE_SOURCE_DIR}/expected/wasm32-webp2")
else()
  message(FATAL_ERROR "Unknown WASI version: ${WASI}")
endif()


add_custom_target(
  check-symbols
  COMMAND
    diff -wur ${expected} ${SYSROOT_SHARE}
  DEPENDS ${defined_symbols} ${undefined_symbols}
)

if(CHECK_SYMBOLS)
  set_target_properties(check-symbols PROPERTIES EXCLUDE_FROM_ALL FALSE)
endif()

# Helper target to auto-update the expectations of symbols
add_custom_target(
  update-symbols
  COMMAND
    cp -r ${SYSROOT_SHARE}/* ${expected}/
  DEPENDS ${defined_symbols} ${undefined_symbols}
)
