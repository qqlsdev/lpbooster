cmake_minimum_required(VERSION 3.14)

project(lpboost LANGUAGES CXX)
include(FetchContent)

FetchContent_Declare(
    inih
    GIT_REPOSITORY https://github.com/benhoyt/inih
    GIT_TAG r62
)

set(INIH_WITH_INIREADER ON CACHE INTERNAL "")

FetchContent_MakeAvailable(inih)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(lpboost main.cpp logger/logger.cpp)

target_link_libraries(lpboost PRIVATE INIReader)

install(TARGETS lpboost DESTINATION bin)
