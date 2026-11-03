set(major 22)
find_program(CLANG_FORMAT NAMES clang-format-${major} clang-format)

if (CLANG_FORMAT)
  message(STATUS "Found clang-format: ${CLANG_FORMAT}")

  execute_process(
    COMMAND ${CLANG_FORMAT} --version
    OUTPUT_VARIABLE CLANG_FORMAT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (NOT (CLANG_FORMAT_VERSION MATCHES "version ${major}\\."))
    message(WARNING "clang-format version ${major}.x is required, found: ${CLANG_FORMAT_VERSION}")
  endif()
endif()

add_custom_target(format)
add_custom_target(format-check)

set(formatted_sources)

function(clang_format_file file)
  if (file MATCHES "__generated" OR
      file MATCHES "(wasip.|webp2)\.c$" OR # Skip auto-generated files
      file MATCHES "\.(s|S)$")     # Skip assembly files
    return()
  endif()
  cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE src)

  # Only format sources primarily authored in this repository itself. This
  # excludes upstream projects like cloudlibc/musl, malloc implementations, etc.
  if (NOT EXISTS ${src}
    OR ${src} MATCHES "cloudlibc"
    OR (${src} MATCHES "libc-top-half" AND NOT ${src} MATCHES "libc-top-half/musl/src/thread/coop-threads")
    OR ${src} MATCHES "fts/musl-fts"
    OR ${src} MATCHES "dlmalloc"
    OR ${src} MATCHES "emmalloc"
    OR ${src} MATCHES "^${CMAKE_BINARY_DIR}")
    return()
  endif()

  string(REPLACE "/" "_" source_target ${file})
  string(REPLACE ":" "_" source_target ${source_target})
  add_custom_target(format-${source_target}
    COMMAND ${CLANG_FORMAT} -i ${src})
  add_custom_target(format-check-${source_target}
    COMMAND ${CLANG_FORMAT} --dry-run -Werror --verbose ${src})
  add_dependencies(format format-${source_target})
  add_dependencies(format-check format-check-${source_target})
endfunction()

function(clang_format_target TARGET)
  get_target_property(SOURCES ${TARGET} SOURCES)

  foreach(SOURCE_FILE ${SOURCES})
    clang_format_file(${SOURCE_FILE})
  endforeach()
endfunction()
