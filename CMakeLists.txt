cmake_minimum_required(VERSION 3.16)
project(ttq LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Header-only library target (interface): the queue lives entirely in the header.
add_library(ttq INTERFACE)
target_include_directories(ttq INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- tests ---
enable_testing()
add_executable(test_spsc tests/test_spsc.cpp)
target_link_libraries(test_spsc PRIVATE ttq)
# threads (for the producer/consumer test)
find_package(Threads REQUIRED)
target_link_libraries(test_spsc PRIVATE Threads::Threads)

add_test(NAME spsc COMMAND test_spsc)
