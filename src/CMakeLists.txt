cmake_minimum_required(VERSION 3.5)

project(sca)

set(CMAKE_CXX_COMPILER "g++")
set(CFLAGS "-std=c++11 -Wall -fopenmp")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CFLAGS}")

add_subdirectory(common)
add_subdirectory(cpa)

set(sca_source_files sca.cpp)
add_executable(sca ${sca_source_files})

target_link_libraries(sca cpa)

message("\n")
