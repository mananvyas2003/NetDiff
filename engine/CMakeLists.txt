# engine/ — temporary single executable until T0.3 extracts libnetdiff.
#
# Windows-only web_dashboard.cpp is intentionally excluded from this target so
# Linux/macOS/Windows CI can share one source list (full removal is T0.2).

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

# Keep MSVC/MinGW happy with UTF-8 sources that contain non-ASCII comments/strings.
if(MSVC)
  target_compile_options(netdiff_engine PRIVATE /utf-8)
endif()
