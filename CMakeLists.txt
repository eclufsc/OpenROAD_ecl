###############################################################################
## BSD 3-Clause License
## Copyright (c) 2019, Parallax Software, Inc.
## All rights reserved.
## [License text continues]
###############################################################################

cmake_minimum_required (VERSION 3.16)

# Use standard target names
cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 NEW)
cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0071 NEW)
cmake_policy(SET CMP0077 NEW)

# Let AUTOMOC and AUTOUIC process GENERATED files.
cmake_policy(SET CMP0071 NEW)

# Interferes with Qt so off by default.
option(LINK_TIME_OPTIMIZATION "Flag to control link time optimization: off by default" OFF)

if (LINK_TIME_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Allow to use external shared boost libraries
option(USE_SYSTEM_BOOST "Use system shared Boost libraries" OFF)

# Allow to use external shared opensta libraries
option(USE_SYSTEM_OPENSTA "Use system shared OpenSTA library" OFF)

# Allow to use external shared abc libraries
option(USE_SYSTEM_ABC "Use system shared ABC library" OFF)

# Allow disabling tests
option(ENABLE_TESTS "Enable OpenROAD tests" ON)

# Allow enabling address sanitizer
option(ASAN "Enable Address Sanitizer" OFF)

project(OpenROAD VERSION 1 LANGUAGES CXX)

set(OPENROAD_HOME ${PROJECT_SOURCE_DIR})

# Default c++ standard used unless otherwise specified in target_compile_features.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "the C++ standard to use for this project")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Gurobi configuration
set(GUROBI_ROOT "/opt/gurobi1103/linux64")  # Change this to your Gurobi path

#list(APPEND CMAKE_MODULE_PATH "${GUROBI_ROOT}/lib/cmake/Gurobi")
#find_package(GUROBI REQUIRED)

# Include Gurobi directories
#include_directories(${GUROBI_INCLUDE_DIRS})

# Link against Gurobi libraries in your executable target
#target_link_libraries(openroad ${GUROBI_LIBRARY} ${GUROBI_CXX_LIBRARY})

# Get version string in OPENROAD_VERSION
if(NOT OPENROAD_VERSION)
    include(GetGitRevisionDescription)
    git_describe(OPENROAD_VERSION)
    string(FIND ${OPENROAD_VERSION} "NOTFOUND" GIT_DESCRIBE_NOTFOUND)
    if(${GIT_DESCRIBE_NOTFOUND} GREATER -1)
        message(WARNING "OpenROAD git describe failed, using sha1 instead")
        get_git_head_revision(GIT_REFSPEC OPENROAD_VERSION)
    endif()
endif()

message(STATUS "OpenROAD version: ${OPENROAD_VERSION}")

# Default to building optimized/release executable.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8.3.0")
        message(FATAL_ERROR "Insufficient gcc version. Found ${CMAKE_CXX_COMPILER_VERSION}, but require  >= 8.3.0.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.0.0")
        message(FATAL_ERROR "Insufficient Clang version. Found ${CMAKE_CXX_COMPILER_VERSION}, but require >= 7.0.0.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.0.0")
        message(FATAL_ERROR "Insufficient AppleClang version. Found ${CMAKE_CXX_COMPILER_VERSION}, but require >= 12.0.0.")
    endif()
else()
    message(WARNING "Compiler ${CMAKE_CXX_COMPILER_ID} is not officially supported.")
endif()

message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "C++ Standard Required: ${CMAKE_CXX_STANDARD_REQUIRED}")
message(STATUS "C++ Extensions: ${CMAKE_CXX_EXTENSIONS}")

# configure a header file to pass some of the CMake settings
configure_file(
  ${OPENROAD_HOME}/include/ord/Version.hh.cmake
  ${OPENROAD_HOME}/include/ord/Version.hh
)

################################################################

# Ask CMake to output a compile_commands.json file for use with things like clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

add_subdirectory(third-party)

if(ENABLE_TESTS)
    enable_testing()
    add_custom_target(build_and_test ${CMAKE_CTEST_COMMAND} --parallel --output-on-failure)
    include(GoogleTest)
endif()

add_subdirectory(src)

# Gurobi variables for subdirectories
set(GUROBI_FOUND TRUE)
set(GUROBI_INCLUDE_DIRS ${GUROBI_ROOT}/include)
set(GUROBI_LIBRARIES Gurobi::gurobi)

