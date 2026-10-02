cmake_minimum_required(VERSION 3.16)
project(mkdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(check src/main.cpp)
add_executable(test_pager src/test_pager.cpp src/pager.cpp)