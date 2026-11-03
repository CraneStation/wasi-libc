# Enable turning this rule off entirely if so desired.
option(BINDINGS_TARGET "Generate bindings target" ON)
if (NOT BINDINGS_TARGET)
  return()
endif()

# If `wit-bindgen` is on the system and has the right version, favor that,
# otherwise download a known good version.
find_program(WIT_BINDGEN_EXECUTABLE NAMES wit-bindgen)
if(WIT_BINDGEN_EXECUTABLE)
  message(STATUS "Found wit-bindgen: ${WIT_BINDGEN_EXECUTABLE}")

  execute_process(
    COMMAND ${WIT_BINDGEN_EXECUTABLE} --version
    OUTPUT_VARIABLE WIT_BINDGEN_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (NOT (WIT_BINDGEN_VERSION MATCHES "0\\.63\\.0"))
    message(WARNING "wit-bindgen version 0.63.0 is required, found: ${WIT_BINDGEN_VERSION}")
    set(WIT_BINDGEN_EXECUTABLE "")
  endif()
endif()

if (NOT WIT_BINDGEN_EXECUTABLE)
  include(ba-download)
  ba_download(
    wit-bindgen
    "https://github.com/bytecodealliance/wit-bindgen"
    "0.63.0"
  )
  ExternalProject_Get_Property(wit-bindgen SOURCE_DIR)
  set(wit_bindgen "${SOURCE_DIR}/wit-bindgen")
else()
  add_custom_target(wit-bindgen)
  set(wit_bindgen ${WIT_BINDGEN_EXECUTABLE})
endif()

set(bottom_half "${CMAKE_SOURCE_DIR}/libc-bottom-half")

add_custom_target(
  bindings-wasip2
  COMMAND
    ${wit_bindgen} c
      --autodrop-borrows yes
      --rename-world wasip2
      --type-section-suffix __wasi_libc
      --world wasi:cli/imports@${wasip2-version}
      --rename wasi:clocks/monotonic-clock@${wasip2-version}=monotonic_clock
      --rename wasi:clocks/wall-clock@${wasip2-version}=wall_clock
      --rename wasi:filesystem/preopens@${wasip2-version}=filesystem_preopens
      --rename wasi:filesystem/types@${wasip2-version}=filesystem
      --rename wasi:io/error@${wasip2-version}=io_error
      --rename wasi:io/poll@${wasip2-version}=poll
      --rename wasi:io/streams@${wasip2-version}=streams
      --rename wasi:random/insecure-seed@${wasip2-version}=random_insecure_seed
      --rename wasi:random/insecure@${wasip2-version}=random_insecure
      --rename wasi:random/random@${wasip2-version}=random
      --rename wasi:sockets/instance-network@${wasip2-version}=instance_network
      --rename wasi:sockets/ip-name-lookup@${wasip2-version}=ip_name_lookup
      --rename wasi:sockets/network@${wasip2-version}=network
      --rename wasi:sockets/tcp-create-socket@${wasip2-version}=tcp_create_socket
      --rename wasi:sockets/tcp@${wasip2-version}=tcp
      --rename wasi:sockets/udp-create-socket@${wasip2-version}=udp_create_socket
      --rename wasi:sockets/udp@${wasip2-version}=udp
      --rename wasi:cli/environment@${wasip2-version}=environment
      --rename wasi:cli/exit@${wasip2-version}=exit
      --rename wasi:cli/stdin@${wasip2-version}=stdin
      --rename wasi:cli/stdout@${wasip2-version}=stdout
      --rename wasi:cli/stderr@${wasip2-version}=stderr
      --rename wasi:cli/terminal-input@${wasip2-version}=terminal_input
      --rename wasi:cli/terminal-output@${wasip2-version}=terminal_output
      --rename wasi:cli/terminal-stdin@${wasip2-version}=terminal_stdin
      --rename wasi:cli/terminal-stdout@${wasip2-version}=terminal_stdout
      --rename wasi:cli/terminal-stderr@${wasip2-version}=terminal_stderr
      ${CMAKE_SOURCE_DIR}/wasi/p2/wit
  COMMAND cmake -E copy wasip2.h ${bottom_half}/headers/public/wasi/__generated_wasip2.h
  COMMAND cmake -E copy wasip2_component_type.o ${bottom_half}/sources
  COMMAND cmake -E copy wasip2.c ${bottom_half}/sources
  DEPENDS wit-bindgen wkg wasip2-wits
)

add_custom_target(
  bindings-wasip3
  COMMAND
    ${wit_bindgen} c
      --autodrop-borrows yes
      --rename-world wasip3
      --type-section-suffix __wasi_libc
      --world wasi:cli/imports@${wasip3-version}
      --generate-threading-helpers
      --rename wasi:clocks/monotonic-clock@${wasip3-version}=monotonic_clock
      --rename wasi:clocks/system-clock@${wasip3-version}=system_clock
      --rename wasi:filesystem/preopens@${wasip3-version}=filesystem_preopens
      --rename wasi:filesystem/types@${wasip3-version}=filesystem
      --rename wasi:random/insecure-seed@${wasip3-version}=random_insecure_seed
      --rename wasi:random/insecure@${wasip3-version}=random_insecure
      --rename wasi:random/random@${wasip3-version}=random
      --rename wasi:sockets/types@${wasip3-version}=sockets
      --rename wasi:sockets/ip-name-lookup@${wasip3-version}=ip_name_lookup
      --rename wasi:cli/environment@${wasip3-version}=environment
      --rename wasi:cli/exit@${wasip3-version}=exit
      --rename wasi:cli/stdin@${wasip3-version}=stdin
      --rename wasi:cli/stdout@${wasip3-version}=stdout
      --rename wasi:cli/stderr@${wasip3-version}=stderr
      --rename wasi:cli/terminal-input@${wasip3-version}=terminal_input
      --rename wasi:cli/terminal-output@${wasip3-version}=terminal_output
      --rename wasi:cli/terminal-stdin@${wasip3-version}=terminal_stdin
      --rename wasi:cli/terminal-stdout@${wasip3-version}=terminal_stdout
      --rename wasi:cli/terminal-stderr@${wasip3-version}=terminal_stderr

      # Disable async bindings generation for some functions which are only
      # ever called synchronously within libc.
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.metadata-hash"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.metadata-hash-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.stat"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.stat-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.get-flags"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.open-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.read-directory"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.create-directory-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.remove-directory-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.unlink-file-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.advise"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.sync-data"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.sync"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.set-size"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.symlink-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.link-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.readlink-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.rename-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.set-times-at"
      "--sync=wasi:filesystem/types@${wasip3-version}#[method]descriptor.set-times"
      "--sync=wasi:sockets/ip-name-lookup@${wasip3-version}#resolve-addresses"
      ${CMAKE_SOURCE_DIR}/wasi/p3/wit
  COMMAND cmake -E copy wasip3.h ${bottom_half}/headers/public/wasi/__generated_wasip3.h
  COMMAND cmake -E copy wasip3_component_type.o ${bottom_half}/sources
  COMMAND cmake -E copy wasip3.c ${bottom_half}/sources
  DEPENDS wit-bindgen wasip3-wits
)

add_custom_target(
  bindings-webp2
  COMMAND
    ${wit_bindgen} c
      --autodrop-borrows yes
      --rename wasm:js/global=webp2
      --rename wasm:js/console=webp2_console
      --rename-world webp2
      --type-section-suffix __wasi_libc
      ${CMAKE_SOURCE_DIR}/web/p2/wit
  COMMAND cmake -E copy webp2.h ${bottom_half}/headers/public/wasi/__generated_webp2.h
  COMMAND cmake -E copy webp2_component_type.o ${bottom_half}/sources
  COMMAND cmake -E copy webp2.c ${bottom_half}/sources
  DEPENDS wit-bindgen
)

add_custom_target(bindings DEPENDS bindings-wasip2 bindings-wasip3 bindings-webp2)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(SED_INPLACE_ARGS -i '')
else()
  set(SED_INPLACE_ARGS -i)
endif()

function(wit_bindgen_edit p)
  add_custom_target(
    bindings-${p}-edit
    COMMAND sed ${SED_INPLACE_ARGS} "'s_#include .${p}\.h._#include \"wasi/${p}.h\"_'" ${bottom_half}/sources/${p}.c
    COMMAND sed ${SED_INPLACE_ARGS} "s/extern void exit_exit/_Noreturn extern void exit_exit/" ${bottom_half}/headers/public/wasi/__generated_${p}.h
    COMMAND sed ${SED_INPLACE_ARGS} "s/extern void __wasm_import_exit_exit/_Noreturn extern void __wasm_import_exit_exit/" ${bottom_half}/sources/${p}.c
    DEPENDS bindings-${p}
  )
  add_dependencies(bindings bindings-${p}-edit)
endfunction()

wit_bindgen_edit(wasip2)
wit_bindgen_edit(wasip3)
wit_bindgen_edit(webp2)
