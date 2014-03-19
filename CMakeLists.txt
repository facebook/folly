cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME folly)
project(${PROJECT_NAME})

# Add our custom cmake modules
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
message(STATUS "Module path: ${CMAKE_MODULE_PATH}")

# options
include(folly/CMake/options.cmake)

# default library
include_directories(${CMAKE_SOURCE_DIR})
file(GLOB Folly_SOURCES folly/*.cpp)
file(GLOB Folly_SOURCES folly/*.cpp)
file(GLOB Folly_detail_HEADERS folly/detail/*.h)
file(GLOB Folly_detail_SOURCES folly/detail/*.cpp)

add_library(folly STATIC ${Folly_SOURCES} ${Folly_detail_SOURCES} ${Folly_HEADERS} ${Folly_detail_HEADERS})
# additional libraries
# experimental
# benchmark
# timeout
# fingerprint

# compiler
include(folly/CMake/compiler.cmake)

# configure
include(folly/CMake/configure.cmake)

# dependencies
include(folly/CMake/deps.cmake)

# install
SET(CMAKE_RELEASE_POSTFIX "")
SET(CMAKE_DEBUG_POSTFIX "-debug")

install(TARGETS folly DESTINATION lib)
install(FILES something.h DESTINATION include)

# Re-generate file options
if(generate)
    find_package(Python)
    
    python_target(something)
    
endif(generate)

# Testing option
if(test)
    add_subdirectory(where/the/tests/are)

    enable_testing()

    # link in gtest targets plus folly
    target_link_libraries(rununittests gtest gtest_main)
    target_link_libraries(rununittests libfolly)

    # make make test work
    add_test(NAME unittests COMMAND rununittests)
    
endif(test)
