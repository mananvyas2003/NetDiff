# engine/ — libnetdiff core + thin debug CLI

set(NETDIFF_LIB_SOURCES
  geometry.cpp
  graph.cpp
  graph_builder.cpp
  graph_debug.cpp
  graph_search.cpp
  interpreter.cpp
  lexer.cpp
  net_database.cpp
  net_resolver.cpp
  parser.cpp
  pin_mapper.cpp
  pin_mapper_debug.cpp
  pin_transform.cpp
  point_hash.cpp
  src/build_graph.cpp
  src/serialize_graph.cpp
)

add_library(netdiff STATIC ${NETDIFF_LIB_SOURCES})

target_include_directories(netdiff
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(netdiff PRIVATE /utf-8)
endif()

# Thin driver over the library (legacy pipeline entry; no web dashboard).
add_executable(netdiff_engine parsernewb.cpp)
target_link_libraries(netdiff_engine PRIVATE netdiff)
if(MSVC)
  target_compile_options(netdiff_engine PRIVATE /utf-8)
endif()

# T0.3 AC: BuildGraph + byte-identical SerializeGraphJson on the ESP32 sample.
add_executable(test_graph_determinism
  ${CMAKE_SOURCE_DIR}/tests/unit/test_graph_determinism.cpp
)
target_link_libraries(test_graph_determinism PRIVATE netdiff)
if(MSVC)
  target_compile_options(test_graph_determinism PRIVATE /utf-8)
endif()

enable_testing()
add_test(
  NAME graph_determinism_esp32
  COMMAND test_graph_determinism
          ${CMAKE_SOURCE_DIR}/tests/fixtures/ESP32_Status_Monitor.kicad_sch
)
