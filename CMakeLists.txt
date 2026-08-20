cmake_minimum_required(VERSION 3.20)
project(silveriris VERSION 0.1.0 LANGUAGES C)

# Game objects are added in PR-05a. This skeleton exists so the tree
# configures and so `cmake --build` can run the asset guard.

include(cmake/sources.cmake)

add_custom_target(guard
    COMMAND ${CMAKE_COMMAND} -E env python3 "${CMAKE_SOURCE_DIR}/tools/guard/no_assets.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Scan tree for ROMs, retail hashes, and oversized blobs"
)

add_custom_target(guard-self-test
    COMMAND ${CMAKE_COMMAND} -E env python3 "${CMAKE_SOURCE_DIR}/tools/guard/no_assets.py" --self-test
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Guard unit tests (temp files; nothing committed)"
)

# PR-09: emcmake cmake -B build/wasm -DPORT_WASM=ON && cmake --build build/wasm
# The native Makefile `make -C native wasm` is the supported path on this tree
# (gcc Makefile + emcc). This option documents the design flag.
option(PORT_WASM "Build game.wasm with Emscripten (use emcmake)" OFF)
if(PORT_WASM)
    message(STATUS "PORT_WASM: prefer `make -C native wasm` (emcc).")
endif()
