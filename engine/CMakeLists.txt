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
  src/project_loader.cpp
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

# ---- Unit tests ----------------------------------------------------------

function(netdiff_add_test name source)
  add_executable(${name} ${CMAKE_SOURCE_DIR}/tests/unit/${source})
  target_link_libraries(${name} PRIVATE netdiff)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(MSVC)
    target_compile_options(${name} PRIVATE /utf-8)
  endif()
endfunction()

netdiff_add_test(test_graph_determinism test_graph_determinism.cpp)
netdiff_add_test(test_sheet_parse test_sheet_parse.cpp)
netdiff_add_test(test_multisheet_load test_multisheet_load.cpp)
netdiff_add_test(test_cross_sheet_net test_cross_sheet_net.cpp)
netdiff_add_test(test_bus_noconnect_parse test_bus_noconnect_parse.cpp)
netdiff_add_test(test_bus_member_expansion test_bus_member_expansion.cpp)
netdiff_add_test(test_robustness test_robustness.cpp)

enable_testing()

set(CORPUS ${CMAKE_SOURCE_DIR}/tests/corpus)
set(FIXTURES ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_test(
  NAME graph_determinism_esp32
  COMMAND test_graph_determinism
          ${FIXTURES}/ESP32_Status_Monitor.kicad_sch
)

add_test(
  NAME sheet_parse_halpi2
  COMMAND test_sheet_parse
          ${CORPUS}/HALPI2/HALPI2.kicad_sch
)

add_test(
  NAME multisheet_load_complex_hierarchy
  COMMAND test_multisheet_load
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
)

add_test(
  NAME cross_sheet_net_pic_programmer
  COMMAND test_cross_sheet_net
          ${CORPUS}/pic_programmer/pic_programmer.kicad_sch
)

add_test(
  NAME bus_noconnect_parse
  COMMAND test_bus_noconnect_parse
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
          ${CORPUS}/interf_u/interf_u.kicad_sch
)

# Intentional failing placeholder until bus member expansion is specified.
add_test(NAME bus_member_expansion_todo COMMAND test_bus_member_expansion)
set_tests_properties(bus_member_expansion_todo PROPERTIES WILL_FAIL TRUE)

add_test(NAME robustness_malformed COMMAND test_robustness)
