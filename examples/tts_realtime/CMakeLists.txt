cmake_minimum_required(VERSION 3.16)
project(soniox_tts_realtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(soniox_tts_realtime main.cpp)
target_link_libraries(soniox_tts_realtime PRIVATE sonioxpp)
