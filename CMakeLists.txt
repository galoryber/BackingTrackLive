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
option(BT_WITH_DEVICE "Build the audio device layer" ON)
option(BT_ENABLE_ASIO "Build ASIO support (needs the Steinberg SDK)" OFF)
option(BT_BUILD_FUZZ  "Build libFuzzer targets"     OFF)
option(BT_SANITIZE    "Build with ASan + UBSan"     OFF)
option(BT_TSAN        "Build with ThreadSanitizer"  OFF)
option(BT_WERROR      "Treat warnings as errors"    ON)

# ---------------------------------------------------------------- warnings
# Link the CRT statically on MSVC so a release .exe runs on a laptop with no
# Visual C++ redistributable installed. The whole point of shipping a binary is
# that the target machine needs nothing.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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

# TSan is the only tool that reliably sees the audio thread racing the UI
# thread. It cannot be combined with ASan.
if(BT_TSAN AND NOT MSVC)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()

# ------------------------------------------------------------------ library
add_library(backtrack STATIC
  src/util/bt_error.c
  src/util/bt_thread.c
  src/json/bt_json.c
  src/audio/bt_wav.c
  src/audio/bt_resample.c
  src/audio/bt_audio.c
  src/audio/bt_dr_impl.c
  src/model/bt_model.c
  src/model/bt_model_json.c
  src/model/bt_model_write.c
  src/engine/bt_engine.c
  src/player/bt_player.c
  src/player/bt_loader.c
)
target_include_directories(backtrack
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs)

# Vendored decoders are held to their own standards, not ours. Silencing them
# here keeps -Werror strict across every line of code we actually wrote.
if(MSVC)
  set(BT_VENDOR_QUIET /W0)
else()
  set(BT_VENDOR_QUIET -w)
endif()
set_source_files_properties(src/audio/bt_dr_impl.c PROPERTIES COMPILE_OPTIONS "${BT_VENDOR_QUIET}")
target_compile_options(backtrack PRIVATE ${BT_WARN})

if(NOT MSVC)
  find_library(BT_LIBM m)
  if(BT_LIBM)
    target_link_libraries(backtrack PUBLIC ${BT_LIBM})
  endif()
endif()

# Threads, not an audio backend: libbacktrack still has no device dependency.
find_package(Threads REQUIRED)
target_link_libraries(backtrack PUBLIC Threads::Threads)

# ------------------------------------------------------------------- tools
add_executable(btrender tools/btrender.c)
target_link_libraries(btrender PRIVATE backtrack)
target_compile_options(btrender PRIVATE ${BT_WARN})

# ------------------------------------------------------------------- tests
if(BT_BUILD_TESTS)
  enable_testing()

  set(BT_TESTS test_json test_wav test_tempo test_model test_engine test_golden test_resample test_player test_audio test_soak test_write)
  foreach(t ${BT_TESTS})
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE backtrack)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(${t} PRIVATE ${BT_WARN})
    add_test(NAME ${t} COMMAND ${t})
  endforeach()

  target_compile_definitions(test_audio PRIVATE
    BT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  target_compile_definitions(test_write PRIVATE
    BT_EXAMPLE_SETLIST="${CMAKE_CURRENT_SOURCE_DIR}/examples/setlist/setlist.json")

  # Threaded tests need pthreads and are POSIX-only for now; the behaviour
  # they cover is platform-independent, and Windows CI still runs everything
  # else.
  if(NOT WIN32)
    add_executable(test_concurrency tests/test_concurrency.c)
    target_link_libraries(test_concurrency PRIVATE backtrack Threads::Threads)
    target_include_directories(test_concurrency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_options(test_concurrency PRIVATE ${BT_WARN})
    add_test(NAME test_concurrency COMMAND test_concurrency)
  endif()

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
  foreach(f fuzz_json fuzz_wav fuzz_setlist fuzz_audio)
    add_executable(${f} fuzz/${f}.c)
    target_link_libraries(${f} PRIVATE backtrack)
    target_compile_options(${f} PRIVATE -fsanitize=fuzzer,address,undefined -g)
    target_link_options(${f}    PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()

# ---------------------------------------------------------------- device
#
# Kept as a SEPARATE target on purpose. libbacktrack has no platform or device
# dependency at all, which is what lets the entire engine be tested on Linux
# against a Windows target. Nothing in tests/ links this.
if(BT_WITH_DEVICE)
  include(FetchContent)

  # Pinned to a master commit rather than v19.7.0: that release is from 2021
  # and declares cmake_minimum_required(VERSION 2.8), which CMake 4 refuses
  # outright. Master requires 3.10 and carries five years of fixes, including
  # to the Windows backends this project depends on.
  set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)

  if(BT_ENABLE_ASIO)
    # The SDK is never in this repository - see docs/asio.md. Supply it out of
    # band and point ASIOSDK_PATH_HINT at it.
    set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
  endif()

  FetchContent_Declare(portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG        17967f32de95f2179e7f7caa9632a79c5d59a2ea
    GIT_SHALLOW    OFF
  )
  FetchContent_MakeAvailable(portaudio)

  add_library(backtrack_device STATIC src/device/bt_device_pa.c)
  target_include_directories(backtrack_device
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(backtrack_device PUBLIC backtrack PRIVATE portaudio)
  target_compile_options(backtrack_device PRIVATE ${BT_WARN})

  add_executable(btplay tools/btplay.c)
  target_link_libraries(btplay PRIVATE backtrack_device)
  target_compile_options(btplay PRIVATE ${BT_WARN})
endif()
