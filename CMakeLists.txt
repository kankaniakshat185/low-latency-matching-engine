cmake_minimum_required(VERSION 3.14)
project(engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable aggressive warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# Configure Debug Sanitizers (ASan and UBSan)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build detected. Enabling AddressSanitizer and UndefinedBehaviorSanitizer.")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer -g")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
endif()

# Core engine library
add_library(engine
    src/engine/MatchingEngine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(engine_tests PRIVATE engine gtest_main)

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

# Add a run target for benchmarks
add_custom_target(run_benchmarks
    COMMAND engine_benchmark
    DEPENDS engine_benchmark
)
