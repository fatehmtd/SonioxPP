cmake_minimum_required(VERSION 3.16)
project(soniox_realtime_translation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(soniox_realtime_translation main.cpp)
target_link_libraries(soniox_realtime_translation
    PRIVATE
    sonioxpp
    spdlog::spdlog
    CLI11::CLI11)
