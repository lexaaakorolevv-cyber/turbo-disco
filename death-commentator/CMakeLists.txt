cmake_minimum_required(VERSION 3.25)
project(DeathCommentator VERSION 1.0.0 LANGUAGES CXX)

option(DEATH_COMMENTATOR_TESTS_ONLY "Build comment engine tests without Geode" OFF)
if(DEATH_COMMENTATOR_TESTS_ONLY)
    enable_testing()
    add_executable(comment_tests tests/comment_tests.cpp)
    target_include_directories(comment_tests PRIVATE src)
    target_compile_features(comment_tests PRIVATE cxx_std_17)
    add_test(NAME death_comment_engine COMMAND comment_tests)
    return()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(DeathCommentator SHARED
    src/main.cpp
    src/CommentOverlay.cpp
    src/CommentPopup.cpp
)
target_include_directories(DeathCommentator PRIVATE src)
if(MSVC)
    target_compile_options(DeathCommentator PRIVATE /utf-8)
endif()
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to your Geode SDK v5.11.1 directory.")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(DeathCommentator DONT_INSTALL)
