cmake_minimum_required(VERSION 3.20)
project(LowLatencyMatchingEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat warnings as errors to enforce code quality
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -O3")

# Core Engine Library
add_library(engine
    src/engine/MatchingEngine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Tests executable
add_executable(engine_tests
    tests/main.cpp
    tests/engine_test.cpp
)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(engine_tests PRIVATE engine)

# Add a run target for tests
add_custom_target(run_tests
    COMMAND engine_tests
    DEPENDS engine_tests
)
