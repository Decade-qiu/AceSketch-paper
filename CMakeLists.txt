cmake_minimum_required(VERSION 3.16)
project(AceSketch LANGUAGES CXX)

add_executable(acesketch main.cpp)
target_compile_features(acesketch PRIVATE cxx_std_17)
set_target_properties(acesketch PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(acesketch PRIVATE -Wall -Wextra -Wpedantic)
