cmake_minimum_required(VERSION 3.16)
project(soniox_async LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(soniox_async main.cpp)
target_link_libraries(soniox_async PRIVATE sonioxpp spdlog::spdlog)
