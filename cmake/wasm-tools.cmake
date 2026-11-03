# Favor `wasm-tools` on the system
find_program(WASM_TOOLS_EXECUTABLE NAMES wasm-tools)
if (NOT WASM_TOOLS_EXECUTABLE)
  include(ba-download)
  ba_download(
    wasm-tools
    "https://github.com/bytecodealliance/wasm-tools"
    "1.261.0"
  )
  ExternalProject_Get_Property(wasm-tools SOURCE_DIR)
  set(wasm_tools "${SOURCE_DIR}/wasm-tools")
else()
  add_custom_target(wasm-tools)
  set(wasm_tools ${WASM_TOOLS_EXECUTABLE})
endif()
