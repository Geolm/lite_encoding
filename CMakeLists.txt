cmake_minimum_required(VERSION 3.25)
project(lite_encoding)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_MACOSX_BUNDLE TRUE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")
endif()

set(CMAKE_C_STANDARD 11)
if(MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/W3 /Zi")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG -Wpedantic -Werror -Wall -Wextra")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -Wpedantic -Werror -Wall -Wextra")
endif()

add_executable(test
    ./test/test.c
    ./lite_encoding.c
)

if(UNIX AND NOT APPLE)
    target_link_libraries(test m)
endif()