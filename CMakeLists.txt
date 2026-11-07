cmake_minimum_required(VERSION 3.14)
project(MMIO LANGUAGES CXX)

add_library(MMIO INTERFACE)
add_library(MMIO::MMIO ALIAS MMIO)

target_include_directories(MMIO INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(MMIO INTERFACE cxx_std_20)
