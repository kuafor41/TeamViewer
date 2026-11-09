cmake_minimum_required(VERSION 3.20)

project(DeskAwake VERSION 0.1.0 LANGUAGES CXX)

option(DESKAWAKE_BUILD_TESTS "Build the core unit tests" ON)

add_library(DeskAwakeCore
    src/motion.cpp
    src/scheduler.cpp
)
target_include_directories(DeskAwakeCore PUBLIC include)
target_compile_features(DeskAwakeCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(DeskAwakeCore PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(DeskAwakeCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(DeskAwake WIN32 src/win32_main.cpp)
    target_link_libraries(DeskAwake PRIVATE DeskAwakeCore user32 shell32)
    target_compile_definitions(DeskAwake PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(DeskAwake PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
else()
    message(STATUS "DeskAwake GUI target is Windows-only; core and tests remain portable.")
endif()

if(DESKAWAKE_BUILD_TESTS)
    enable_testing()
    add_executable(DeskAwakeCoreTests tests/core_tests.cpp)
    target_link_libraries(DeskAwakeCoreTests PRIVATE DeskAwakeCore)
    target_compile_features(DeskAwakeCoreTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(DeskAwakeCoreTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(DeskAwakeCoreTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DeskAwakeCoreTests COMMAND DeskAwakeCoreTests)
endif()
