cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(mozcapsicum)

set(MOZCAPSICUM_FILES
    mozcapsicum.cpp
    flags.cpp
    )

include_directories(include)

add_library(mozcapsicum STATIC ${MOZCAPSICUM_FILES})
target_include_directories(mozcapsicum INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
