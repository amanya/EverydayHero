cmake_minimum_required(VERSION 3.31)
project(everyday)

set(CMAKE_CXX_STANDARD 20)

add_executable(everyday code/sdl_everyday.cpp)

find_package(SDL3 REQUIRED)
include_directories(${SDL3_INCLUDE_DIR})
target_link_libraries(everyday PRIVATE SDL3::SDL3)

target_compile_definitions(everyday PRIVATE
        EVERYDAY_SLOW=1
        EVERYDAY_INTERNAL=1
)

