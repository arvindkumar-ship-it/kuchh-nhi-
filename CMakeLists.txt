cmake_minimum_required(VERSION 3.16)
project(mkdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

add_library(mkdb_core STATIC
    src/lexer.cpp src/parser.cpp src/schema.cpp
    src/slotted_page.cpp src/btree_node.cpp src/btree.cpp
    src/pager.cpp src/index.cpp src/executor.cpp src/database.cpp src/database_dml.cpp)
target_include_directories(mkdb_core PUBLIC src)

# TCP server (sockets + threads)
add_library(mkdb_net STATIC src/server_core.cpp)
target_link_libraries(mkdb_net mkdb_core)
if(WIN32)
  target_link_libraries(mkdb_net ws2_32)
else()
  target_link_libraries(mkdb_net Threads::Threads)
endif()

add_executable(mkdb src/main.cpp)
target_link_libraries(mkdb mkdb_core)

add_executable(mkdb_server src/server.cpp)
target_link_libraries(mkdb_server mkdb_net)

enable_testing()
foreach(t test_slotted test_node test_split test_btree test_pager test_page_disk
          test_lexer test_parser test_schema test_database test_dml
          test_engine test_index test_storage)
  add_executable(${t} src/${t}.cpp)
  target_link_libraries(${t} mkdb_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(test_server src/test_server.cpp)
target_link_libraries(test_server mkdb_net)
add_test(NAME test_server COMMAND test_server)