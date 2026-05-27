cmake_minimum_required(VERSION 3.20)

project(lpboost CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(lpboost main.cpp)

install(TARGETS lpboost DESTINATION bin)
