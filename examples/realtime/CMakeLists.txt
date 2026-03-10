cmake_minimum_required(VERSION 3.16)
project(soniox_realtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

add_executable(soniox_realtime main.cpp)
target_link_libraries(soniox_realtime
    PRIVATE
    sonioxpp
    spdlog::spdlog)
