cmake_minimum_required(VERSION 3.10)
project(pg_proxy)
set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/ProxyServer/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/ProxyServer/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Parser/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Parser/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Logger/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Logger/*.hpp"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/ProxyServer/
    ${CMAKE_SOURCE_DIR}/Logger/
    ${CMAKE_SOURCE_DIR}/Parser/
)

add_executable(
    pg_proxy main.cpp 
    ${SOURCES}
)

