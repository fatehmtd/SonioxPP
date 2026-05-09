cmake_minimum_required(VERSION 3.16)
project(soniox_async_timestamps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(soniox_async_timestamps main.cpp)
target_link_libraries(soniox_async_timestamps
    PRIVATE
    sonioxpp
    spdlog::spdlog
    CLI11::CLI11)
