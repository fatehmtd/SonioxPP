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
# Options
# ---------------------------------------------------------------------------

option(SONIOX_BUILD_EXAMPLES "Build example applications" ON)
message(STATUS "SONIOX_BUILD_EXAMPLES: ${SONIOX_BUILD_EXAMPLES}")

# ---------------------------------------------------------------------------
# Third-party dependencies (all fetched via CMake — no vcpkg)
# ---------------------------------------------------------------------------

# spdlog
if(NOT TARGET spdlog::spdlog)
    find_package(spdlog 1.15 CONFIG QUIET)
endif()
if(NOT TARGET spdlog::spdlog)
    fetchcontent_declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.0
        GIT_SHALLOW    TRUE
    )
    fetchcontent_makeavailable(spdlog)
endif()

# nlohmann/json
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.12 CONFIG QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    fetchcontent_declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
        GIT_SHALLOW    TRUE
    )
    fetchcontent_makeavailable(nlohmann_json)
endif()

# mbedTLS — used as the TLS backend by curl and libwebsockets on every
# non-Windows platform (Windows uses Schannel instead). Fetching it from
# source keeps the build self-contained instead of depending on a system/
# Homebrew-installed OpenSSL. We never fetch blindly: first look for a
# target or libraries the consuming project already provides (its own
# 'mbedtls' target, MBEDTLS_INCLUDE_DIR/MBEDTLS_LIBRARY/etc. passed on the
# command line, or a copy staged in the NDK sysroot for Android) and only
# fall back to building our own from source if nothing turns up.
option(SONIOX_FETCH_MBEDTLS "Fetch and build mbedTLS from source when none is found" ON)

if(NOT WIN32 AND NOT TARGET mbedtls)
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h)
    find_library(MBEDTLS_LIBRARY mbedtls)
    find_library(MBEDX509_LIBRARY mbedx509)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto)

    if(NOT (MBEDTLS_INCLUDE_DIR AND MBEDTLS_LIBRARY AND MBEDX509_LIBRARY AND MBEDCRYPTO_LIBRARY))
        if(NOT SONIOX_FETCH_MBEDTLS)
            message(FATAL_ERROR
                "SONIOX_FETCH_MBEDTLS is OFF but no mbedTLS was found. Provide an "
                "'mbedtls' target, or set MBEDTLS_INCLUDE_DIR, MBEDTLS_LIBRARY, "
                "MBEDX509_LIBRARY and MBEDCRYPTO_LIBRARY yourself.")
        endif()

        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
        fetchcontent_declare(
            mbedtls
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG        v3.6.2
            GIT_SHALLOW    TRUE
        )
        fetchcontent_makeavailable(mbedtls)
        set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
        set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
        set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
        set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
    endif()
endif()

# libcurl — built from source when no system copy is found; TLS backend is
# platform-dependent. sonioxpp links the plain "libcurl" target name (what our
# from-source build produces). A system CURL exports "CURL::libcurl" instead,
# so bridge it with an alias — note a system build won't necessarily match the
# TLS backend/feature flags configured below, since those only apply when we
# build curl ourselves.
if(NOT TARGET libcurl)
    find_package(CURL 8.11 QUIET)
    if(TARGET CURL::libcurl)
        add_library(libcurl ALIAS CURL::libcurl)
    endif()
endif()

if(NOT TARGET libcurl)
    # NOTE: curl's own option is "USE_LIBIDN2" (no CURL_ prefix) — unlike most
    # of its other USE_* flags. Getting this name wrong silently no-ops it and
    # curl links libidn2 anyway if pkg-config finds it on the host.
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    # We only ever link the "libcurl_static" target in-tree via FetchContent; skip its
    # install(EXPORT) rules so they don't have to resolve mbedtls (whose own install/
    # export is disabled) into an export set.
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
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
        set(CURL_USE_MBEDTLS  OFF CACHE BOOL "" FORCE)
    else()
        set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
        set(CURL_USE_MBEDTLS  ON  CACHE BOOL "" FORCE)
    endif()

    fetchcontent_declare(
        CURL
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
    )
    fetchcontent_makeavailable(CURL)
endif()

# libwebsockets — cross-platform WebSocket transport (non-Windows only)
if(NOT WIN32)
    find_package(libwebsockets QUIET)
endif()

if(NOT WIN32 AND NOT TARGET websockets)
    set(LWS_WITH_SHARED           OFF CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TESTAPPS      ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_SERVER   ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_CLIENT   ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_PING     ON  CACHE BOOL "" FORCE)
    set(LWS_WITHOUT_TEST_ECHO     ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_SSL              ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_CLIENT           ON  CACHE BOOL "" FORCE)
    set(LWS_WITH_SERVER           OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_HTTP2            OFF CACHE BOOL "" FORCE)
    set(LWS_ROLE_WS               ON  CACHE BOOL "" FORCE)
    set(LWS_ROLE_H1               ON  CACHE BOOL "" FORCE)
    set(LWS_ROLE_H2               OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBUV            OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBEVENT         OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_LIBEV            OFF CACHE BOOL "" FORCE)
    # We only ever link the "websockets" target in-tree via FetchContent; skip its
    # install(EXPORT) rules so they don't have to resolve every dependency (e.g.
    # mbedtls, whose own install/export is disabled) into an export set.
    set(LWS_WITH_EXPORT_LWSTARGETS OFF CACHE BOOL "" FORCE)

    # Build libwebsockets against the mbedTLS fetched above for curl, instead of
    # a system/Homebrew-installed OpenSSL.
    set(LWS_WITH_OPENSSL   OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_BORINGSSL OFF CACHE BOOL "" FORCE)
    set(LWS_WITH_MBEDTLS   ON  CACHE BOOL "" FORCE)
    # libwebsockets' mbedTLS backend skips its own find_path/find_library lookup
    # when these are already defined — point it at the target above.
    set(LWS_MBEDTLS_INCLUDE_DIRS "${MBEDTLS_INCLUDE_DIR}" CACHE STRING "" FORCE)
    set(LWS_MBEDTLS_LIBRARIES "${MBEDTLS_LIBRARY}" "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}" CACHE STRING "" FORCE)

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

    if(TARGET websockets)
        # lib/tls/mbedtls/mbedtls-extensions.c calls mbedtls_x509_get_name(), which
        # mbedTLS declares only in the private library/x509_internal.h (not shipped
        # publicly). The symbol still links fine — libwebsockets v4.3.3 predates
        # mbedTLS 3.6's header split — so just relax the implicit-declaration error
        # for this one vendored target instead of patching mbedTLS or lws sources.
        # (Only needed for our own from-source build, not a system-provided target.)
        target_compile_options(websockets PRIVATE -Wno-implicit-function-declaration)
    endif()
endif()

# miniaudio is only needed by the realtime microphone example
if(SONIOX_BUILD_EXAMPLES)
    find_path(SONIOX_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h)
    if(SONIOX_MINIAUDIO_INCLUDE_DIR)
        set(miniaudio_SOURCE_DIR "${SONIOX_MINIAUDIO_INCLUDE_DIR}")
    else()
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
    endif()

    # cppcodec — header-only base64/hex codec, used by the tts_realtime example
    find_path(SONIOX_CPPCODEC_INCLUDE_DIR NAMES cppcodec/base64_rfc4648.hpp)
    if(NOT SONIOX_CPPCODEC_INCLUDE_DIR)
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
        set(SONIOX_CPPCODEC_INCLUDE_DIR "${cppcodec_SOURCE_DIR}")
    endif()
    add_library(cppcodec INTERFACE)
    target_include_directories(cppcodec INTERFACE ${SONIOX_CPPCODEC_INCLUDE_DIR})

    # CLI11 — header-only argument parsing library, used by all examples
    if(NOT TARGET CLI11::CLI11)
        find_package(CLI11 2.4 CONFIG QUIET)
    endif()
    if(NOT TARGET CLI11::CLI11)
        fetchcontent_declare(
            cli11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
            GIT_TAG        v2.4.2
            GIT_SHALLOW    TRUE
        )
        fetchcontent_makeavailable(cli11)
    endif()
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
