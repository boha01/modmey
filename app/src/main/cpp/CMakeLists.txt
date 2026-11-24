cmake_minimum_required(VERSION 3.22.1)
project("modmenu")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES native-lib.cpp)
if(EXISTS ${CMAKE_SOURCE_DIR}/and64/And64InlineHook.cpp)
    list(APPEND SOURCES and64/And64InlineHook.cpp)
    set_source_files_properties(and64/And64InlineHook.cpp PROPERTIES
            COMPILE_FLAGS "-Wno-register -Wno-c++17-extensions -Wno-deprecated-declarations -Wno-unused-function")
    message(STATUS "modmenu: using real And64InlineHook for ${ANDROID_ABI}")
endif()

add_library(modmenu SHARED ${SOURCES})

target_compile_options(modmenu PRIVATE
        -O2
        -fvisibility=hidden
        -fexceptions
        -frtti
        -pthread
        -Wall
        -Wno-unused-parameter)

find_library(log-lib log)
target_include_directories(modmenu PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(modmenu ${log-lib} android dl)
