cmake_minimum_required(VERSION 3.16)
project(linkchat CXX)
set(CMAKE_CXX_STANDARD 20)
file(GLOB SRC CONFIGURE_DEPENDS src/*.cpp src/util/*.cpp src/net/*.cpp)
add_executable(linkchat ${SRC})

# Ensure headers in src/ and subfolders are on the include path
target_include_directories(linkchat PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/net ${CMAKE_SOURCE_DIR}/src/util)