cmake_minimum_required(VERSION 3.16)
project(sonioxpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Boost, OpenSSL: declared in vcpkg.json — install with vcpkg, or provide
# system packages (brew install boost openssl / apt install libboost-dev libssl-dev).

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Boost — Boost.Beast and Boost.Asio are header-only; no compiled components needed.
# Modern Boost (1.70+) ships as a single config package: request headers only.
find_package(Boost 1.74 REQUIRED)

# System threading library — required by Boost.Asio internally
find_package(Threads REQUIRED)

# OpenSSL (required for wss:// and https://)
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

add_library(sonioxpp STATIC
    src/realtime_client.cpp
    src/async_client.cpp
)

target_include_directories(sonioxpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sonioxpp
    PUBLIC
        Boost::boost        # header-only interface (Beast + Asio)
        Threads::Threads    # system threading (pthreads / Win32 threads)
        OpenSSL::SSL
        OpenSSL::Crypto
    PRIVATE
        spdlog::spdlog              # used only in .cpp files, not public headers
        nlohmann_json::nlohmann_json # used only in .cpp files, not public headers
)

# Windows socket libraries
if(WIN32)
    target_link_libraries(sonioxpp PUBLIC ws2_32 wsock32 crypt32)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
# Installs the compiled library archive and public headers.
# No CMake targets export is provided because spdlog and nlohmann_json are
# pulled in via FetchContent and cannot be part of an install export set.
# Consume this library via FetchContent or git submodule instead.

include(GNUInstallDirs)

install(
    TARGETS sonioxpp
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    DIRECTORY include/sonioxpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
