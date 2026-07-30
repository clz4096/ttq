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

# --- ThreadSanitizer build (opt-in) ---
# The stress test can pass even when the memory ordering is wrong. x86 has a
# strong memory model that almost never reorders loads and stores, so a missing
# or too-weak acquire/release goes unnoticed here and would only surface on a
# weaker-ordered processor such as ARM. ThreadSanitizer catches the bug on any
# machine because it checks the C++ happens-before relationships directly,
# instead of trusting what the hardware happened to do at run time.
# It is off by default because it slows the stress test roughly 10x. Turn it on
# for continuous integration, or before changing anything in the ordering:
#   cmake -S . -B build-tsan -DTTQ_TSAN=ON
#   cmake --build build-tsan
#   ctest --test-dir build-tsan --output-on-failure
option(TTQ_TSAN "Build a ThreadSanitizer-instrumented copy of the tests" OFF)
if(TTQ_TSAN)
  add_executable(test_spsc_tsan tests/test_spsc.cpp)
  target_link_libraries(test_spsc_tsan PRIVATE ttq Threads::Threads)
  # -O1 with frame pointers: optimized enough to expose real reorderings, while
  # still producing a readable stack trace when a race is reported.
  target_compile_options(test_spsc_tsan PRIVATE -fsanitize=thread -g -O1 -fno-omit-frame-pointer)
  target_link_options(test_spsc_tsan PRIVATE -fsanitize=thread)
  # Recent Linux kernels randomize the memory layout more aggressively than
  # ThreadSanitizer expects, and its fixed shadow-memory region can collide with
  # that and abort the run with "unexpected memory mapping". Launching the test
  # through "setarch -R" turns that randomization off for the test process and
  # avoids the collision. It is harmless on systems that do not have the problem,
  # and skipped when setarch is not present (for example macOS, where
  # ThreadSanitizer runs without it).
  find_program(SETARCH_EXE setarch)
  if(SETARCH_EXE)
    add_test(NAME spsc_tsan COMMAND ${SETARCH_EXE} -R $<TARGET_FILE:test_spsc_tsan>)
  else()
    add_test(NAME spsc_tsan COMMAND test_spsc_tsan)
  endif()
  # Stop at the first reported race instead of relying only on the exit code.
  set_tests_properties(spsc_tsan PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1")
endif()
