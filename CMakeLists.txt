cmake_minimum_required(VERSION 3.14)
project(agent-cpp VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AGENT_CPP_BUILD_TESTS "Build agent.cpp tests" OFF)
option(AGENT_CPP_BUNDLED_LLAMA "Bundle llama.cpp (vs. find installed)" ON)

set(LLAMA_CPP_DIR "" CACHE PATH "Path to existing llama.cpp directory (leave empty to use submodule)")

if(AGENT_CPP_BUNDLED_LLAMA)
    # Use bundled llama.cpp (submodule or custom path)
    if(LLAMA_CPP_DIR)
        if(NOT EXISTS "${LLAMA_CPP_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "LLAMA_CPP_DIR is set to '${LLAMA_CPP_DIR}' but no CMakeLists.txt found there.")
        endif()
        set(LLAMA_SOURCE_DIR "${LLAMA_CPP_DIR}")
        message(STATUS "Using custom llama.cpp from: ${LLAMA_SOURCE_DIR}")
    else()
        set(LLAMA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp")
        if(NOT EXISTS "${LLAMA_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "llama.cpp submodule not found at ${LLAMA_SOURCE_DIR}\n"
                "Please initialize submodules with:\n"
                "  git submodule update --init --recursive\n"
                "Or provide a custom path with -DLLAMA_CPP_DIR=/path/to/llama.cpp"
            )
        endif()
        message(STATUS "Using llama.cpp submodule from: ${LLAMA_SOURCE_DIR}")
    endif()

    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    set(LLAMA_HTTPLIB OFF CACHE BOOL "" FORCE)
    set(LLAMA_LLGUIDANCE OFF CACHE BOOL "" FORCE)

    add_subdirectory(${LLAMA_SOURCE_DIR} ${CMAKE_BINARY_DIR}/llama.cpp)
else()
    # Use system-installed llama.cpp
    find_package(llama REQUIRED)
    message(STATUS "Using system-installed llama.cpp")
endif()

add_library(model STATIC src/model.cpp)
add_library(agent-cpp::model ALIAS model)
target_include_directories(model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/common>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/ggml/include>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/agent-cpp>
)
target_link_libraries(model PUBLIC common llama)
target_compile_features(model PUBLIC cxx_std_17)

add_library(agent STATIC src/agent.cpp)
add_library(agent-cpp::agent ALIAS agent)
target_include_directories(agent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/common>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/ggml/include>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${LLAMA_SOURCE_DIR}/vendor>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/agent-cpp>
)
target_link_libraries(agent PUBLIC model common llama)
target_compile_features(agent PUBLIC cxx_std_17)

if(AGENT_CPP_BUILD_TESTS)
    enable_testing()

    add_executable(test_tool tests/test_tool.cpp)
    target_include_directories(test_tool PRIVATE src tests)
    target_link_libraries(test_tool PRIVATE common llama)
    target_compile_features(test_tool PRIVATE cxx_std_17)

    add_executable(test_callbacks tests/test_callbacks.cpp)
    target_include_directories(test_callbacks PRIVATE
        src
        tests
        ${LLAMA_SOURCE_DIR}/common
        ${LLAMA_SOURCE_DIR}/ggml/include
        ${LLAMA_SOURCE_DIR}/include
        ${LLAMA_SOURCE_DIR}/vendor
    )
    target_link_libraries(test_callbacks PRIVATE agent model common llama)
    target_compile_features(test_callbacks PRIVATE cxx_std_17)

    add_executable(test_model tests/test_model.cpp)
    target_include_directories(test_model PRIVATE
        src
        tests
        ${LLAMA_SOURCE_DIR}/common
        ${LLAMA_SOURCE_DIR}/ggml/include
        ${LLAMA_SOURCE_DIR}/include
        ${LLAMA_SOURCE_DIR}/vendor
    )
    target_link_libraries(test_model PRIVATE model common llama)
    target_compile_features(test_model PRIVATE cxx_std_17)

    add_test(NAME ToolTests COMMAND test_tool)
    add_test(NAME CallbacksTests COMMAND test_callbacks)
    add_test(NAME ModelTests COMMAND test_model)

    message(STATUS "Tests enabled. Run with: ctest or ./test_tool")
endif()

option(AGENT_CPP_INSTALL "Generate install target" OFF)

if(AGENT_CPP_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install public headers
    install(FILES
        src/agent.h
        src/callbacks.h
        src/error.h
        src/model.h
        src/tool.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/agent-cpp
    )

    # Install libraries with export set
    install(TARGETS agent model
        EXPORT agent-cpp-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Generate and install the export targets file
    install(EXPORT agent-cpp-targets
        FILE agent-cpp-targets.cmake
        NAMESPACE agent-cpp::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/agent-cpp
    )

    # Generate the version file for find_package version checking
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/agent-cpp-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Generate the config file from template
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/agent-cpp-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/agent-cpp-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/agent-cpp
    )

    # Install the CMake config files
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/agent-cpp-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/agent-cpp-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/agent-cpp
    )

    message(STATUS "Install target enabled. Install with: cmake --install build --prefix /your/path")
endif()
