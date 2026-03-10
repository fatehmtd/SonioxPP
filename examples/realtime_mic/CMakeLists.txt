cmake_minimum_required(VERSION 3.16)
project(soniox_realtime_mic LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# miniaudio — single-header audio I/O library
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_executable(soniox_realtime_mic main.cpp)

target_include_directories(soniox_realtime_mic PRIVATE
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(soniox_realtime_mic
    PRIVATE
        sonioxpp
        spdlog::spdlog
)

# ---------------------------------------------------------------------------
# Platform audio backends required by miniaudio
# ---------------------------------------------------------------------------
if(APPLE)
    target_link_libraries(soniox_realtime_mic PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(soniox_realtime_mic PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
    # Link ALSA if present (Linux default backend)
    find_library(ALSA_LIB asound)
    if(ALSA_LIB)
        target_link_libraries(soniox_realtime_mic PRIVATE ${ALSA_LIB})
    endif()
elseif(WIN32)
    target_link_libraries(soniox_realtime_mic PRIVATE ole32 winmm)
endif()
