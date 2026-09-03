cmake_minimum_required(VERSION 3.28)

project(
    avernal-asset-compiler
    VERSION 0.1.0
    DESCRIPTION "Command-line asset compiler for the Avernal game engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-assets-render/CMakeLists.txt")
    set(AVERNAL_ASSETS_RENDER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-assets-render")
else()
    set(AVERNAL_ASSETS_RENDER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../avernal-assets-render")
endif()

if(NOT EXISTS "${AVERNAL_ASSETS_RENDER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "avernal-assets-render not found at:\n"
        "  ${AVERNAL_ASSETS_RENDER_SOURCE_DIR}\n"
        "Place avernal-assets-render as a sibling of this project.")
endif()

FetchContent_Declare(
    avernal_assets_render
    SOURCE_DIR "${AVERNAL_ASSETS_RENDER_SOURCE_DIR}"
    EXCLUDE_FROM_ALL
)

set(AVERNAL_CORE_BUILD_DEMO OFF)
set(AVERNAL_ASSETS_RENDER_BUILD_TESTS OFF)
set(AVERNAL_TERRAIN_BUILD_DEMO OFF)
set(AVERNAL_TERRAIN_BUILD_TESTS OFF)

FetchContent_MakeAvailable(avernal_assets_render)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-terrain/CMakeLists.txt")
    set(AVERNAL_TERRAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../avernal-terrain")
else()
    set(AVERNAL_TERRAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../avernal-terrain")
endif()

if(NOT EXISTS "${AVERNAL_TERRAIN_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "avernal-terrain not found at:\n"
        "  ${AVERNAL_TERRAIN_SOURCE_DIR}\n"
        "Place avernal-terrain as a sibling of this project.")
endif()

FetchContent_Declare(
    avernal_terrain
    SOURCE_DIR "${AVERNAL_TERRAIN_SOURCE_DIR}"
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(avernal_terrain)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinygltf)

add_library(avernal-asset-compiler)
add_library(avernal::asset_compiler ALIAS avernal-asset-compiler)

target_sources(
    avernal-asset-compiler
    PRIVATE
        src/compile_gltf.cpp
        src/compile_heightmap.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/avernal/asset_compiler/compile.hpp
)

target_include_directories(avernal-asset-compiler SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
target_link_libraries(avernal-asset-compiler PUBLIC avernal::assets_render avernal::terrain)
target_compile_features(avernal-asset-compiler PUBLIC cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(avernal-asset-compiler PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(avernal-asset-compiler PRIVATE /W4)
endif()

add_executable(avernal-asset-compiler-cli src/main.cpp)
set_target_properties(avernal-asset-compiler-cli PROPERTIES OUTPUT_NAME avernal-asset-compiler)
target_link_libraries(avernal-asset-compiler-cli PRIVATE avernal::asset_compiler)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(avernal-asset-compiler-cli PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(avernal-asset-compiler-cli PRIVATE /W4)
endif()

if(PROJECT_IS_TOP_LEVEL)
    option(AVERNAL_ASSET_COMPILER_BUILD_TESTS "Build Avernal Asset Compiler unit tests" ON)
else()
    option(AVERNAL_ASSET_COMPILER_BUILD_TESTS "Build Avernal Asset Compiler unit tests" OFF)
endif()

if(AVERNAL_ASSET_COMPILER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
