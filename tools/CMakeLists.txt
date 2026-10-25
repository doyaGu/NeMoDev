cmake_minimum_required(VERSION 3.25)
project(NeMoDevResourceTools LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ExtractResources reads Windows PE resources; build it on Windows.")
endif()

# This project needs neither the SDK nor the editor's extracted resources.
add_executable(ExtractResources ExtractResources.cpp)
target_compile_features(ExtractResources PRIVATE cxx_std_17)
target_include_directories(ExtractResources PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(ExtractResources PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
