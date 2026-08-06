cmake_minimum_required(VERSION 3.14)
project(engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Aggressive warnings, applied via target_compile_options to our own
# targets only (see below) rather than injected globally through
# CMAKE_CXX_FLAGS. A global -Werror also reaches vendored dependencies
# (GoogleTest, fetched below) we don't control the warning history of —
# a compiler upgrade or a new GTest release could then break the build on
# a warning that has nothing to do with this project's code.
set(ENGINE_WARNING_FLAGS -Wall -Wextra -Werror)

# Optional coverage instrumentation, enabled via -DENGINE_COVERAGE=ON
# (used by the CI coverage job). Kept out of normal Debug/Release builds
# since --coverage disables optimization and adds per-line counters.
option(ENGINE_COVERAGE "Build with --coverage instrumentation for gcov/gcovr" OFF)

# Configure Debug Sanitizers (ASan and UBSan)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build detected. Enabling AddressSanitizer and UndefinedBehaviorSanitizer.")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer -g")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
endif()

if(ENGINE_COVERAGE)
    message(STATUS "Coverage instrumentation enabled (--coverage).")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# Core engine library
add_library(engine
    src/engine/MatchingEngine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(engine PRIVATE ${ENGINE_WARNING_FLAGS})

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(engine_tests
    tests/engine_test.cpp
    tests/replay_test.cpp
    tests/differential_test.cpp
    tests/structures_test.cpp
)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(engine_tests PRIVATE engine gtest_main)
target_compile_options(engine_tests PRIVATE ${ENGINE_WARNING_FLAGS})

# Add a run target for tests
add_custom_target(run_tests
    COMMAND engine_tests
    DEPENDS engine_tests
)

# Benchmark executable
add_executable(engine_benchmark
    src/benchmark/main.cpp
)
target_include_directories(engine_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(engine_benchmark PRIVATE engine)
target_compile_options(engine_benchmark PRIVATE ${ENGINE_WARNING_FLAGS})

# Add a run target for benchmarks
add_custom_target(run_benchmarks
    COMMAND engine_benchmark
    DEPENDS engine_benchmark
)

# Phase 4 comparative study: benchmarks multiple OrderBook variants
# side by side. Deliberately separate from engine_benchmark (see
# compare_variants.cpp) so the README's published 1.0/1.0.1 numbers are
# never at risk of changing because of this.
add_executable(variant_benchmark
    src/benchmark/compare_variants.cpp
)
target_include_directories(variant_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(variant_benchmark PRIVATE engine)
target_compile_options(variant_benchmark PRIVATE ${ENGINE_WARNING_FLAGS})
