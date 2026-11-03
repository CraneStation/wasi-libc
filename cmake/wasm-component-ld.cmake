option(USE_WASM_COMPONENT_LD "Use wasm-component-ld as the linker for WebAssembly components" ON)
if (NOT USE_WASM_COMPONENT_LD)
  return()
endif()

set(WASM_COMPONENT_LD_REQUIRED_VERSION "0.5.31")

find_program(WASM_COMPONENT_LD_EXECUTABLE NAMES wasm-component-ld)

# A system-provided `wasm-component-ld` is only usable if it's new enough, so
# probe its version and ignore it if it's too old.
if (WASM_COMPONENT_LD_EXECUTABLE)
  execute_process(
    COMMAND ${WASM_COMPONENT_LD_EXECUTABLE} --version
    OUTPUT_VARIABLE wasm_component_ld_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (NOT wasm_component_ld_version_output MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(STATUS "Ignoring ${WASM_COMPONENT_LD_EXECUTABLE}: could not parse version from `${wasm_component_ld_version_output}`")
    unset(WASM_COMPONENT_LD_EXECUTABLE CACHE)
  elseif (CMAKE_MATCH_1 VERSION_LESS WASM_COMPONENT_LD_REQUIRED_VERSION)
    message(STATUS "Ignoring ${WASM_COMPONENT_LD_EXECUTABLE}: version ${CMAKE_MATCH_1} is older than ${WASM_COMPONENT_LD_REQUIRED_VERSION}")
    unset(WASM_COMPONENT_LD_EXECUTABLE CACHE)
  endif()
endif()

if (NOT WASM_COMPONENT_LD_EXECUTABLE)
  include(ba-download)
  ba_download(
    wasm-component-ld
    "https://github.com/bytecodealliance/wasm-component-ld"
    "v${WASM_COMPONENT_LD_REQUIRED_VERSION}"
  )
  ExternalProject_Get_Property(wasm-component-ld SOURCE_DIR)
  set(WASM_COMPONENT_LD_EXECUTABLE "${SOURCE_DIR}/wasm-component-ld")
  add_dependencies(sysroot_inc wasm-component-ld)
endif()

add_link_options(-fuse-ld=${WASM_COMPONENT_LD_EXECUTABLE})
