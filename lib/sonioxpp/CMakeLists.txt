cmake_minimum_required(VERSION 3.16)
project(sonioxpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# libcurl and OpenSSL: declared in vcpkg.json — install with vcpkg, or provide
# system packages (brew install curl openssl / apt install libcurl4-openssl-dev libssl-dev).

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

# IXWebSocket — WebSocket client library (used by realtime_client)
set(USE_TLS      ON  CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# libcurl — HTTPS client (used by async_client)
find_package(CURL REQUIRED)

# System threading library
find_package(Threads REQUIRED)

# OpenSSL (required for wss:// via IXWebSocket and https:// via libcurl)
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
        Threads::Threads
    PRIVATE
        CURL::libcurl
        ixwebsocket
        OpenSSL::SSL
        OpenSSL::Crypto
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

# Windows socket libraries
if(WIN32)
    target_link_libraries(sonioxpp PRIVATE ws2_32 wsock32 crypt32)
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
