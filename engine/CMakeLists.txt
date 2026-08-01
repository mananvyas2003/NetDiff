# engine/ — temporary single executable until T0.3 extracts libnetdiff.
#
# Core sources only. Windows-only web dashboard and VS project files live under
# legacy/windows/ (reference), not in this target.

set(NETDIFF_ENGINE_SOURCES
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
  parsernewb.cpp
  pin_mapper.cpp
  pin_mapper_debug.cpp
  pin_transform.cpp
  point_hash.cpp
)

add_executable(netdiff_engine ${NETDIFF_ENGINE_SOURCES})

target_include_directories(netdiff_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(netdiff_engine PRIVATE /utf-8)
endif()
