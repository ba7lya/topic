# SPDX-License-Identifier: MIT

# --- cmake version ---
cmake_minimum_required(VERSION 3.21)

# --- project ---
project(ba7lya.topic-engine VERSION 0.2.1 LANGUAGES CXX)

# --- build options ---
option(${PROJECT_NAME}_BUILD_BENCHMARK "Build benchmarks" OFF)
option(${PROJECT_NAME}_BUILD_EXAMPLE   "Build examples"   OFF)
option(${PROJECT_NAME}_BUILD_TEST      "Build Test"       OFF)

# --- features ---
# The Rust original gates the trie and the router behind its default `router` feature and the
# match cache behind `lru-cache`; both compile to a `--cfg`-less build by default. The port turns
# `feature = "x"` into `BA7LYA_TOPIC_ENGINE_NO_X`, so the macros only ever subtract.
option(${PROJECT_NAME}_ROUTER     "Build the topic matcher and router"       ON)
option(${PROJECT_NAME}_LRU_CACHE  "Build the LRU match cache"                ON)

# --- includes ---
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- dependencies ---
# spdlog is the only hard dependency. GoogleTest and google benchmark are only pulled in by the
# optional test / benchmark targets, and looking for them unconditionally would make a plain
# library build fail on a machine that has no vcpkg tree.
find_package(spdlog CONFIG REQUIRED)

if(${PROJECT_NAME}_BUILD_BENCHMARK)
    find_package(benchmark CONFIG REQUIRED)
endif()

if(${PROJECT_NAME}_BUILD_TEST)
    find_package(GTest CONFIG REQUIRED)
endif()

# --- os ---
if (WIN32)
    add_definitions(-DWINVER=0x0A00)       # Windows 10
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
    add_definitions(-DNOMINMAX)            # disable C2589 min()/max() error
endif()

# --- output layout ---
# Every configuration keeps its artifacts in its own directory. That matters on Windows, where the
# runtime artifacts have to share a directory for the loader to find the DLLs beside the
# executables, and it gives the CI smoke steps one stable path to run from.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# --- modules ---
add_subdirectory(thirdparty)

# --- target ---
add_library(${PROJECT_NAME})
add_library(ba7lya::topic_engine ALIAS ${PROJECT_NAME})
add_library(ba7lya::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE
        src/cache_strategy.cxx
        src/error.cxx
        src/qos.cxx
        src/topic_match.cxx
        src/topic_pattern_item.cxx
        src/topic_pattern_path.cxx
)

if(${PROJECT_NAME}_ROUTER)
    target_sources(${PROJECT_NAME} PRIVATE src/topic_matcher.cxx src/topic_router.cxx)
else()
    target_compile_definitions(${PROJECT_NAME} PUBLIC BA7LYA_TOPIC_ENGINE_NO_ROUTER)
endif()

if(NOT ${PROJECT_NAME}_LRU_CACHE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE)
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ba7lya/${PROJECT_NAME}/>
)

target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# --- install ---
# Consumed as a dependency the headers nest under `include/<author>/<package>/`, so several
# packages from the same author can share one install prefix without colliding.
set(${PROJECT_NAME}_PUBLIC_HEADERS
    include/cache_strategy.hxx
    include/error.hxx
    include/qos.hxx
    include/result.hxx
    include/topic_engine.hxx
    include/topic_match.hxx
    include/topic_matcher.hxx
    include/topic_pattern_item.hxx
    include/topic_pattern_path.hxx
    include/topic_router.hxx
    include/topic_wellformedness.hxx
)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    FILES ${${PROJECT_NAME}_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ba7lya/${PROJECT_NAME}
)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT ${PROJECT_NAME}-targets
    FILE ${PROJECT_NAME}-targets.cmake
    NAMESPACE ba7lya::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(
    FILES
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

# --- benchmark ---
if(${PROJECT_NAME}_BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

# --- examples ---
if(${PROJECT_NAME}_BUILD_EXAMPLE)
    add_subdirectory(examples)
endif()

# --- unit tests ---
if(${PROJECT_NAME}_BUILD_TEST)
    enable_testing()
    add_subdirectory(tests)
endif()
