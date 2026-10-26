cmake_minimum_required(VERSION 3.24)

project(nvme_driver
    VERSION 1.0.0
    LANGUAGES CXX
)

#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_EXPORT_COMPILE_COMMANDS AND NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    file(CREATE_LINK
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
        SYMBOLIC)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
endif()

add_subdirectory(src)

file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
    "src/*.cpp" "include/*.hpp" "include/*.h"
)

add_custom_target(
    format
    COMMAND clang-format -i ${ALL_CXX_SOURCE_FILES}
    COMMENT "Formatting all C++ files..."
)
