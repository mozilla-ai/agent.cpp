cmake_minimum_required(VERSION 3.14)
project(context-engineering-example VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/agent-cpp)

add_executable(context-engineering-example context-engineering.cpp)

target_include_directories(context-engineering-example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${LLAMA_SOURCE_DIR}/common
    ${LLAMA_SOURCE_DIR}/ggml/include
    ${LLAMA_SOURCE_DIR}/include
    ${LLAMA_SOURCE_DIR}/vendor
)

target_link_libraries(context-engineering-example PRIVATE agent-cpp::agent common llama)
target_compile_features(context-engineering-example PRIVATE cxx_std_17)

message(STATUS "Context engineering example configured.")
