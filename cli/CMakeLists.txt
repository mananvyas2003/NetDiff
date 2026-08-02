# cli/ — the netdiff command-line surface (01_ARCHITECTURE.md §2.3, §4).
#
# Formatting, config loading and git plumbing live here, never in libnetdiff:
# per §5 the surfaces only call the library and present its results.

set(NETDIFF_CLI_SOURCES
  src/config.cpp
  src/format.cpp
  src/git.cpp
)

add_library(netdiff_cli_core STATIC ${NETDIFF_CLI_SOURCES})
target_link_libraries(netdiff_cli_core PUBLIC netdiff)
target_include_directories(netdiff_cli_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(netdiff_cli_core PRIVATE /utf-8)
endif()

function(netdiff_add_cli_test name source)
  add_executable(${name} ${CMAKE_SOURCE_DIR}/tests/unit/${source})
  target_link_libraries(${name} PRIVATE netdiff_cli_core)
  if(MSVC)
    target_compile_options(${name} PRIVATE /utf-8)
  endif()
endfunction()

add_executable(netdiff_cli src/main.cpp)
set_target_properties(netdiff_cli PROPERTIES OUTPUT_NAME netdiff)
target_link_libraries(netdiff_cli PRIVATE netdiff_cli_core)
if(MSVC)
  target_compile_options(netdiff_cli PRIVATE /utf-8)
elseif(MINGW)
  target_link_options(netdiff_cli PRIVATE -static-libgcc -static-libstdc++)
elseif(UNIX AND NOT APPLE)
  # Prefer a relocatable Linux binary without libgcc/libstdc++ .so deps.
  target_link_options(netdiff_cli PRIVATE -static-libgcc -static-libstdc++)
endif()

include(GNUInstallDirs)
install(TARGETS netdiff_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

netdiff_add_cli_test(test_format test_format.cpp)
netdiff_add_cli_test(test_cli_config test_cli_config.cpp)

add_test(NAME cli_config_and_normalization COMMAND test_cli_config)

add_test(
  NAME format_golden
  COMMAND test_format ${CMAKE_SOURCE_DIR}/tests/golden
)

# 06_TESTING_QA.md §6: SARIF output must validate against the 2.1.0 schema.
# The schema is vendored (tests/schema/) so this runs offline.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  # 04 §1.3/§1.5: commands, exit codes, --git and --staged against a real repo.
  add_test(
    NAME cli_end_to_end
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_e2e.py
            $<TARGET_FILE:netdiff_cli>
  )

  add_test(
    NAME format_sarif_schema
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/format/validate_sarif.py
            ${CMAKE_SOURCE_DIR}/tests/golden/format_sample.sarif.json
  )

  # T2.1: packaging smoke — archive must contain a runnable `netdiff version`.
  add_test(
    NAME package_binary_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/package_binary.py
            --binary $<TARGET_FILE:netdiff_cli>
            --name netdiff-package-smoke
            --format zip
            --out ${CMAKE_BINARY_DIR}/package-smoke
  )

  # T2.2: GitHub Action helper script smoke (markdown + sarif + exit_code).
  add_test(
    NAME action_run_diff_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ci/test_action_run_diff.py
            $<TARGET_FILE:netdiff_cli>
  )

  # T2.3: pre-commit entry + GitLab template presence / staged gate behavior.
  add_test(
    NAME pre_commit_hook_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ci/test_pre_commit_hook.py
            $<TARGET_FILE:netdiff_cli>
  )

  # T2.4: KiCad plugin DiffResult → navigation rows (no KiCad runtime).
  add_test(
    NAME plugin_model_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/plugin/test_plugin_model.py
  )

  # T2.5: WASM vs CLI compare (skips cleanly if wasm/node unavailable).
  add_test(
    NAME wasm_cli_compare_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/wasm/compare_wasm_to_cli.py
            --cli $<TARGET_FILE:netdiff_cli>
            --wasm-js ${CMAKE_SOURCE_DIR}/web/demo/wasm/netdiff.js
            --before ${CMAKE_SOURCE_DIR}/tests/corpus/ecc83/ecc83-pp.kicad_sch
            --after ${CMAKE_SOURCE_DIR}/tests/corpus/ecc83/ecc83-pp.kicad_sch
  )
else()
  message(WARNING "Python3 not found: SARIF schema validation test disabled")
endif()
