cmake_minimum_required(VERSION 3.9)
project(cfuture)

message(STATUS "CMake C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMake C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")

if (NOT DEFINED CMAKE_CXX_STANDARD)
    message(STATUS "The CPP standard is not set. Default: C++14")
    set(CMAKE_CXX_STANDARD 14)
else ()
    message(STATUS "Checking the CPP standard. C++${CMAKE_CXX_STANDARD}")
    if (CMAKE_CXX_STANDARD LESS 14)
        message(FATAL_ERROR "C++14 or higher is required.")
    endif ()
endif ()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

include_directories(.)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(install_gtest OFF)
set(install_gmock OFF)
set(build_gmock ON)

add_subdirectory(googletest)

add_executable(cfuture_test test/test.cc)

target_compile_options(cfuture_test PUBLIC -g -Wall -fsanitize=address)
target_link_options(cfuture_test PUBLIC -fsanitize=address)

target_link_libraries(cfuture_test gmock gtest gtest_main)

include(CTest)
add_test(NAME cfuture_test COMMAND cfuture_test)
