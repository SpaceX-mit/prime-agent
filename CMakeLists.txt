cmake_minimum_required(VERSION 3.20)
project(prime-agent
    VERSION 0.1.0
    DESCRIPTION "C/C++ reimplementation of the pi AI coding agent"
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
endif()

# -----------------------------------------------------------------------------
# Platform detection
# -----------------------------------------------------------------------------
include(CheckSymbolExists)
include(cmake/PiPlatform.cmake)
pi_detect_platform()

message(STATUS "prime-agent ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  platform   : ${PI_PLATFORM_NAME}")
message(STATUS "  arch       : ${PI_ARCH}")

# -----------------------------------------------------------------------------
# Third-party (vendored, header-only)
# -----------------------------------------------------------------------------
add_library(pi_thirdparty_headers INTERFACE)
target_include_directories(pi_thirdparty_headers SYSTEM INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party
)
target_compile_definitions(pi_thirdparty_headers INTERFACE
    DOCTEST_CONFIG_DISABLE_IMPLEMENTATION_AFTER_MAIN=1
)
add_library(pi::thirdparty ALIAS pi_thirdparty_headers)

# -----------------------------------------------------------------------------
# OpenSSL (required, system)
# -----------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)

# -----------------------------------------------------------------------------
# pi_core (no external deps except nlohmann/json)
# -----------------------------------------------------------------------------
add_subdirectory(libs/core)

# -----------------------------------------------------------------------------
# pi_http (depends on pi_core + OpenSSL)
# -----------------------------------------------------------------------------
add_subdirectory(libs/http)

# -----------------------------------------------------------------------------
# pi_ai (depends on pi_core + pi_http)
# -----------------------------------------------------------------------------
add_subdirectory(libs/ai)

# -----------------------------------------------------------------------------
# pi_agent / pi_tui / pi_coding (placeholders)
# -----------------------------------------------------------------------------
add_subdirectory(libs/agent)
add_subdirectory(libs/tui)
add_subdirectory(libs/coding)

# -----------------------------------------------------------------------------
# Apps
# -----------------------------------------------------------------------------
add_subdirectory(apps/pi)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
