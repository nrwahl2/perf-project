cmake_minimum_required(VERSION 3.18)
project(igraph_test)

set(CMAKE_BUILD_TYPE Debug)

find_package(igraph 1.0.0 REQUIRED)

add_executable(igraph_test igraph_test.c)
target_link_libraries(igraph_test PUBLIC igraph::igraph)
