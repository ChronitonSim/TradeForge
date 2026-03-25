# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(TradeForge VERSION 1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enforce C++20 for std::hardware_destructive_interference_size and concepts
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force Release mode for aggressive -O3 optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "Setting build type to 'Release' (-O3 optimizations enabled)")
endif()

# Enable strict warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-interference-size)

include_directories(include)
find_package(Threads REQUIRED)

# Phase 1: SPSC Queue Test
add_executable(01_spsc_queue src/01_spsc_queue/main.cpp)
target_link_libraries(01_spsc_queue PRIVATE Threads::Threads)