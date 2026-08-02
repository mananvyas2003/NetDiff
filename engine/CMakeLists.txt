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
  src/bus.cpp
  src/build_graph.cpp
  src/diff.cpp
  src/diff_types.cpp
  src/json_emit.cpp
  src/project_loader.cpp
  src/serialize_diff.cpp
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
netdiff_add_test(test_diff_types test_diff_types.cpp)
netdiff_add_test(test_diff_components test_diff_components.cpp)
netdiff_add_test(test_diff_nets test_diff_nets.cpp)
netdiff_add_test(test_diff_pins test_diff_pins.cpp)
netdiff_add_test(test_diff_gate test_diff_gate.cpp)
netdiff_add_test(test_diff_invariant test_diff_invariant.cpp)

enable_testing()

set(CORPUS ${CMAKE_SOURCE_DIR}/tests/corpus)
set(FIXTURES ${CMAKE_SOURCE_DIR}/tests/fixtures)
set(GOLDEN ${CMAKE_SOURCE_DIR}/tests/golden)

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

add_test(NAME bus_member_expansion COMMAND test_bus_member_expansion)

add_test(NAME robustness_malformed COMMAND test_robustness)

add_test(
  NAME diff_types_golden
  COMMAND test_diff_types ${GOLDEN}/diff_all_change_types.json
)

add_test(
  NAME diff_component_matching
  COMMAND test_diff_components
          ${CORPUS}/multichannel/multichannel_mixer.kicad_sch
          ${CORPUS}/ecc83/ecc83-pp.kicad_sch
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
)

add_test(
  NAME diff_pin_and_topology
  COMMAND test_diff_pins
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
          ${CORPUS}/multichannel/multichannel_mixer.kicad_sch
          ${CORPUS}/HALPI2/HALPI2.kicad_sch
)

add_test(
  NAME diff_net_matching
  COMMAND test_diff_nets
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
          ${CORPUS}/interf_u/interf_u.kicad_sch
          ${CORPUS}/HALPI2/HALPI2.kicad_sch
)

add_test(NAME diff_significance_and_gate COMMAND test_diff_gate)

add_test(
  NAME diff_cosmetic_invariant
  COMMAND test_diff_invariant
          ${CORPUS}/complex_hierarchy/complex_hierarchy.kicad_sch
          ${CORPUS}/pic_programmer/pic_programmer.kicad_sch
          ${CORPUS}/multichannel/multichannel_mixer.kicad_sch
          ${CORPUS}/interf_u/interf_u.kicad_sch
          ${CORPUS}/ecc83/ecc83-pp.kicad_sch
          ${CORPUS}/stickhub/StickHub.kicad_sch
          ${CORPUS}/kit-dev-coldfire-xilinx_5213/kit-dev-coldfire-xilinx_5213.kicad_sch
          ${CORPUS}/video/video.kicad_sch
          ${CORPUS}/HALPI2/HALPI2.kicad_sch
)
