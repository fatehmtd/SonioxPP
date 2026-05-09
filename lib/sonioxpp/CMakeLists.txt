cmake_minimum_required(VERSION 3.16)
project(sonioxpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

if(WIN32)
    set(SONIOX_WS_TRANSPORT_SRC src/transport/winhttp_websocket_transport.cpp)
else()
    set(SONIOX_WS_TRANSPORT_SRC src/transport/lws_websocket_transport.cpp)
endif()

add_library(sonioxpp STATIC
    src/realtime_client.cpp
    src/async_client.cpp
    src/stt_client.cpp
    src/tts_client.cpp
    src/auth_client.cpp
    src/transport/curl_http_transport.cpp
    ${SONIOX_WS_TRANSPORT_SRC}
)

target_include_directories(sonioxpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sonioxpp
    PRIVATE
        libcurl
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_compile_definitions(sonioxpp PRIVATE _WIN32_WINNT=0x0602 WIN32_LEAN_AND_MEAN)
    target_link_libraries(sonioxpp PRIVATE winhttp ws2_32 crypt32)
else()
    target_link_libraries(sonioxpp PRIVATE websockets)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------

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
