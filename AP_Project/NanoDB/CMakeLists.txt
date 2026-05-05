cmake_minimum_required(VERSION 3.10)
project(NanoDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W3 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-function)
endif()

set(NANODB_SOURCES
    src/String.cpp
    src/DBValue.cpp
    src/Tokenizer.cpp
    src/Parser.cpp
    src/Evaluator.cpp
    src/Graph.cpp
    src/DiskManager.cpp
    src/BufferPool.cpp
    src/Logger.cpp
    src/Table.cpp
    src/Engine.cpp
)

add_library(nanodb_core STATIC ${NANODB_SOURCES})
# Use -iquote (not -I) so <string.h> from <cstring> is not shadowed by
# the project's String.h on case-insensitive Windows filesystems.
target_include_directories(nanodb_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
if(NOT MSVC)
    target_compile_options(nanodb_core PUBLIC -iquote ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(test_runner  PRIVATE -iquote ${CMAKE_SOURCE_DIR}/include)
endif()

add_executable(test_runner test_runner.cpp)
target_link_libraries(test_runner PRIVATE nanodb_core)

add_executable(gen_tpch tools/gen_tpch.cpp)

# Convenience: copy queries.txt next to the binary at build time
add_custom_command(TARGET test_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/queries.txt
        $<TARGET_FILE_DIR:test_runner>/queries.txt
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:test_runner>/data
)
