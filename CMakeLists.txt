cmake_minimum_required(VERSION 3.16)
project(SonioxPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(SONIOX_BUILD_EXAMPLES "Build example applications" ON)
message(STATUS "SONIOX_BUILD_EXAMPLES: ${SONIOX_BUILD_EXAMPLES}")

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_subdirectory(lib/sonioxpp)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(SONIOX_BUILD_EXAMPLES)
    add_subdirectory(examples/realtime)
    add_subdirectory(examples/async)
    add_subdirectory(examples/realtime_mic)
endif()
