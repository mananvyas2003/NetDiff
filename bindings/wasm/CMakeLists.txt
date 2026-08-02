# bindings/wasm — Emscripten build of libnetdiff (docs/01_ARCHITECTURE.md §2.5, T2.5).
#
# Configure with the Emscripten toolchain, e.g.:
#   emcmake cmake -S . -B build-wasm -DNETDIFF_BUILD_WASM=ON
#   cmake --build build-wasm --target netdiff_wasm

cmake_minimum_required(VERSION 3.16)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "bindings/wasm requires an Emscripten toolchain (emcmake).")
endif()

add_executable(netdiff_wasm netdiff_wasm.cpp)
target_link_libraries(netdiff_wasm PRIVATE netdiff)

target_link_options(netdiff_wasm PRIVATE
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s EXPORT_NAME=createNetdiffModule"
  "SHELL:-s ENVIRONMENT=web,node"
  "SHELL:-s FILESYSTEM=1"
  "SHELL:-s FORCE_FILESYSTEM=1"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s EXPORTED_FUNCTIONS=['_netdiff_diff_paths','_netdiff_free','_netdiff_version','_malloc','_free']"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','UTF8ToString','stringToUTF8','lengthBytesUTF8','getValue']"
)

set_target_properties(netdiff_wasm PROPERTIES
  OUTPUT_NAME "netdiff"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wasm"
)

# Copy JS glue + wasm binary next to the static demo when present.
add_custom_command(TARGET netdiff_wasm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_SOURCE_DIR}/web/demo/wasm"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:netdiff_wasm>/netdiff.js"
          "${CMAKE_SOURCE_DIR}/web/demo/wasm/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:netdiff_wasm>/netdiff.wasm"
          "${CMAKE_SOURCE_DIR}/web/demo/wasm/"
  COMMENT "Copying WASM artifacts into web/demo/wasm/"
)
