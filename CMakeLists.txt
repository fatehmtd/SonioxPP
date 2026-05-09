cmake_minimum_required(VERSION 3.16)
project(SonioxPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(POLICY CMP0169)
    # Keep FetchContent_Populate for header-only dependencies without adding subdirectories.
    cmake_policy(SET CMP0169 OLD)
endif()

# ---------------------------------------------------------------------------
# On Apple Silicon, Homebrew installs to /opt/homebrew which is not a default
# linker search path. Add it so pkg-config-discovered deps (e.g. libidn2) link.
# ---------------------------------------------------------------------------
if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HOMEBREW_PREFIX)
        link_directories("${HOMEBREW_PREFIX}/lib")
    endif()
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(SONIOX_BUILD_EXAMPLES "Build example applications" ON)
message(STATUS "SONIOX_BUILD_EXAMPLES: ${SONIOX_BUILD_EXAMPLES}")

# ---------------------------------------------------------------------------
# Third-party dependencies (all fetched via CMake — no vcpkg)
# ---------------------------------------------------------------------------

# spdlog
fetchcontent_declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE
)
fetchcontent_makeavailable(spdlog)

# nlohmann/json
fetchcontent_declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
fetchcontent_makeavailable(nlohmann_json)

# libcurl — built from source; TLS backend is platform-dependent
set(CURL_USE_LIBIDN2   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE    OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(ENABLE_MANUAL     OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2  OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
else()
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL  ON  CACHE BOOL "" FORCE)
endif()
fetchcontent_declare(
    CURL
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_1
    GIT_SHALLOW    TRUE
)
fetchcontent_makeavailable(CURL)

# libwebsockets — cross-platform WebSocket transport (non-Windows only)
if(NOT WIN32)
    if(APPLE)
        execute_process(COMMAND brew --prefix openssl
            OUTPUT_VARIABLE HOMEBREW_OPENSSL_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(HOMEBREW_OPENSSL_PREFIX)
            set(OPENSSL_ROOT_DIR "${HOMEBREW_OPENSSL_PREFIX}" CACHE PATH "" FORCE)
        endif()
    endif()

    set(LWS_WITH_SHARED           OFF CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TESTAPPS      ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_SERVER   ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_CLIENT   ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_PING     ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_ECHO     ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_SSL              ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_OPENSSL          ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_BORINGSSL        OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_MBEDTLS          OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_CLIENT           ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_SERVER           OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_HTTP2            OFF CACHE BOOL "" FORCE)
    set(LWS_ROLE_WS               ON  CACHE BOOL "" FORCE)
    set(LWS_ROLE_H1               ON  CACHE BOOL "" FORCE)
    set(LWS_ROLE_H2               OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBUV            OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBEVENT         OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBEV            OFF CACHE BOOL "" FORCE)
    fetchcontent_declare(
        libwebsockets
        GIT_REPOSITORY https://github.com/warmcat/libwebsockets.git
        GIT_TAG        v4.3.3
        GIT_SHALLOW    TRUE
    )
    # libwebsockets v4.3.3 declares cmake_minimum_required < 3.5, which CMake 4.x removed.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    fetchcontent_makeavailable(libwebsockets)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# miniaudio is only needed by the realtime microphone example
if(SONIOX_BUILD_EXAMPLES)
    fetchcontent_declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG        0.11.21
        GIT_SHALLOW    TRUE
    )
    fetchcontent_getproperties(miniaudio)
    if(NOT miniaudio_POPULATED)
        fetchcontent_populate(miniaudio)
    endif()

    # cppcodec — header-only base64/hex codec, used by the tts_realtime example
    fetchcontent_declare(
        cppcodec
        GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
        GIT_TAG        v0.2
        GIT_SHALLOW    TRUE
    )
    fetchcontent_getproperties(cppcodec)
    if(NOT cppcodec_POPULATED)
        fetchcontent_populate(cppcodec)
    endif()
    add_library(cppcodec INTERFACE)
    target_include_directories(cppcodec INTERFACE ${cppcodec_SOURCE_DIR})

    # CLI11 — header-only argument parsing library, used by all examples
    fetchcontent_declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2
        GIT_SHALLOW    TRUE
    )
    fetchcontent_makeavailable(cli11)
endif()

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
    add_subdirectory(examples/tts_rest)
    add_subdirectory(examples/tts_realtime)
    add_subdirectory(examples/realtime_translation)
    add_subdirectory(examples/async_timestamps)
    add_subdirectory(examples/tts_multiplex)
    add_subdirectory(examples/tts_streaming_text)
endif()
