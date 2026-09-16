cmake_minimum_required(VERSION 3.15)
project(rapoo-tray LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    res/app.rc
)

add_executable(rapoo-tray WIN32 ${SOURCES})

target_compile_definitions(rapoo-tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(rapoo-tray PRIVATE
    setupapi
    hid
    user32
    gdi32
    shell32
    advapi32
)

if(MSVC)
    target_compile_options(rapoo-tray PRIVATE /W4 /O2 /MT)
    target_link_options(rapoo-tray PRIVATE /SUBSYSTEM:WINDOWS)
elseif(MINGW)
    target_compile_options(rapoo-tray PRIVATE -O3 -Wall)
    target_link_options(rapoo-tray PRIVATE -mwindows -municode -s -static)
endif()
