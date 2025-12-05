cmake_minimum_required(VERSION 3.14)
project(memory-example VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This CMakeLists.txt is for standalone builds of this example.
# For building all examples together, use the root CMakeLists.txt with -DAGENT_CPP_BUILD_EXAMPLES=ON

# Add the parent agent.cpp project (which handles llama.cpp submodule/custom path)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/agent-cpp)

# Memory CLI executable
add_executable(memory-example memory.cpp)

target_include_directories(memory-example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${LLAMA_SOURCE_DIR}/common
    ${LLAMA_SOURCE_DIR}/ggml/include
    ${LLAMA_SOURCE_DIR}/include
    ${LLAMA_SOURCE_DIR}/vendor
)

target_link_libraries(memory-example PRIVATE agent model common llama)
target_compile_features(memory-example PRIVATE cxx_std_17)

message(STATUS "Memory as tool example configured.")
message(STATUS "Usage: ./memory-example -m <path-to-model.gguf> [-f <memory-file.txt>]")
