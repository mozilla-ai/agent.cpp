cmake_minimum_required(VERSION 3.14)
project(tracing-example VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch OpenTelemetry C++ SDK
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# OpenTelemetry requires protobuf and curl for OTLP HTTP exporter
find_package(Protobuf REQUIRED)
find_package(CURL REQUIRED)

FetchContent_Declare(
    opentelemetry-cpp
    GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES "third_party/opentelemetry-proto" "third_party/nlohmann-json"
)

set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)
set(WITH_OTLP_GRPC OFF CACHE BOOL "" FORCE)
set(WITH_OTLP_FILE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
set(WITH_FUNC_TESTS OFF CACHE BOOL "" FORCE)
set(OTELCPP_MAINTAINER_MODE OFF CACHE BOOL "" FORCE)
set(WITH_ZIPKIN OFF CACHE BOOL "" FORCE)
set(WITH_PROMETHEUS OFF CACHE BOOL "" FORCE)
set(WITH_ELASTICSEARCH OFF CACHE BOOL "" FORCE)
set(WITH_ETW OFF CACHE BOOL "" FORCE)
set(WITH_OPENTRACING OFF CACHE BOOL "" FORCE)
set(WITH_ABSEIL OFF CACHE BOOL "" FORCE)
set(WITH_GSL OFF CACHE BOOL "" FORCE)
set(BUILD_W3CTRACECONTEXT_TEST OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES_HTTP OFF CACHE BOOL "" FORCE)
set(OPENTELEMETRY_INSTALL OFF CACHE BOOL "" FORCE)


FetchContent_MakeAvailable(opentelemetry-cpp)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/agent-cpp)

add_executable(tracing-example tracing.cpp)

target_include_directories(tracing-example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${LLAMA_SOURCE_DIR}/common
    ${LLAMA_SOURCE_DIR}/ggml/include
    ${LLAMA_SOURCE_DIR}/include
    ${LLAMA_SOURCE_DIR}/vendor
    ${opentelemetry-cpp_SOURCE_DIR}/exporters/otlp/include
    ${opentelemetry-cpp_SOURCE_DIR}/sdk/include
    ${opentelemetry-cpp_SOURCE_DIR}/api/include
)

target_link_libraries(tracing-example PRIVATE
    agent
    model
    common
    llama
    opentelemetry_trace
    opentelemetry_exporter_otlp_http
)

target_compile_features(tracing-example PRIVATE cxx_std_17)

message(STATUS "Tracing callback example configured.")
message(STATUS "Usage: ./tracing-example -m <path-to-model.gguf> [-e <otlp-endpoint>]")
