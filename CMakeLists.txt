# Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
cmake_minimum_required(VERSION 3.16)
project(BackingTrackLive C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(BT_BUILD_TESTS "Build the test suite"        ON)
option(BT_BUILD_FUZZ  "Build libFuzzer targets"     OFF)
option(BT_SANITIZE    "Build with ASan + UBSan"     OFF)
option(BT_WERROR      "Treat warnings as errors"    ON)

# ---------------------------------------------------------------- warnings
if(MSVC)
  set(BT_WARN /W4)
  if(BT_WERROR)
    list(APPEND BT_WARN /WX)
  endif()
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  set(BT_WARN -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
              -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes
              -Wcast-qual -Wvla)
  if(BT_WERROR)
    list(APPEND BT_WARN -Werror)
  endif()
endif()

if(BT_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# ------------------------------------------------------------------ library
add_library(backtrack STATIC
  src/util/bt_error.c
  src/json/bt_json.c
  src/audio/bt_wav.c
  src/model/bt_model.c
  src/model/bt_model_json.c
  src/engine/bt_engine.c
)
target_include_directories(backtrack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(backtrack PRIVATE ${BT_WARN})

if(NOT MSVC)
  find_library(BT_LIBM m)
  if(BT_LIBM)
    target_link_libraries(backtrack PUBLIC ${BT_LIBM})
  endif()
endif()

# ------------------------------------------------------------------- tools
add_executable(btrender tools/btrender.c)
target_link_libraries(btrender PRIVATE backtrack)
target_compile_options(btrender PRIVATE ${BT_WARN})

# ------------------------------------------------------------------- tests
if(BT_BUILD_TESTS)
  enable_testing()

  set(BT_TESTS test_json test_wav test_tempo test_model test_engine test_golden)
  foreach(t ${BT_TESTS})
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE backtrack)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(${t} PRIVATE ${BT_WARN})
    add_test(NAME ${t} COMMAND ${t})
  endforeach()

  # Real-time safety: wraps the allocator and asserts that a render performs
  # no allocation at all. GNU ld / lld only, so not built under MSVC.
  if(NOT MSVC)
    add_executable(test_rtsafe tests/test_rtsafe.c)
    target_link_libraries(test_rtsafe PRIVATE backtrack)
    target_include_directories(test_rtsafe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(test_rtsafe PRIVATE ${BT_WARN})
    target_link_options(test_rtsafe PRIVATE
      -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
    add_test(NAME test_rtsafe COMMAND test_rtsafe)
  endif()
endif()

# -------------------------------------------------------------------- fuzz
if(BT_BUILD_FUZZ)
  foreach(f fuzz_json fuzz_wav fuzz_setlist)
    add_executable(${f} fuzz/${f}.c)
    target_link_libraries(${f} PRIVATE backtrack)
    target_compile_options(${f} PRIVATE -fsanitize=fuzzer,address,undefined -g)
    target_link_options(${f}    PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()
