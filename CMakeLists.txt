cmake_minimum_required(VERSION 3.16)
project(netdiff VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# T2.1: on MSVC, link the static CRT so Release `netdiff.exe` needs no VCRUNTIME DLLs.
if(MSVC)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

# T2.5: WASM builds skip CLI/tests (Emscripten toolchain via emcmake).
option(NETDIFF_BUILD_WASM "Build the Emscripten WASM module" OFF)

enable_testing()

add_subdirectory(engine)

if(EMSCRIPTEN OR NETDIFF_BUILD_WASM)
  add_subdirectory(bindings/wasm)
else()
  add_subdirectory(cli)
endif()
