cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME folly)
project(${PROJECT_NAME})

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")
SET(CMAKE_RELASE_POSTFIX "")
SET(CMAKE_DEBUG_POSTFIX "-debug")

# Options for folly
option(test "Build all tests" OFF)
option(experimental "Build folly's experimental library" OFF)
option(timeout "Build folly's timeout functionality" OFF)
option(fingerprint "Build folly's fingerprint functionality" OFF)
option(generate "Re-generate source files - requires python" OFF)

# Platform checks
INCLUDE(CheckIncludeFiles)
#CHECK_INCLUDE_FILES(FOLLY_HAVE_CLOCK_GETTIME
#CHECK_INCLUDE_FILES (fcntl.h features.h inttypes.h limits.h stdint.h stdlib.h string.h sys/time.h unistd.h mutex.h malloc.h emmintrin.h byteswap.h bits/functexcept.h)
#CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
 
# default library
include_directories(${CMAKE_SOURCE_DIR})
file(GLOB Folly_SOURCES folly/*.cpp)
file(GLOB Folly_HEADERS folly/*.h)

add_library(folly STATIC ${Folly_SOURCES} ${Folly_HEADERS})
# additional libraries
# experimental
# benchmark
# timeout
# fingerprint

# platform and compiler support/configuration testing

# Find dep libraries for folly
# current list: pthread, glog, gflags, double-conversion, boost (thread, regex, system)
# boost checks
find_package(Boost 1.49.0 COMPONENTS system thread regex REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

find_package(PTHREADS REQUIRED)
if(PTHREADS_FOUND)
    include_directories(${PTHREADS_INCLUDE_DIR})
    target_link_libraries(folly ${PTHREADS_LIBRARY})
endif(PTHREADS_FOUND)

find_package(GFLAGS)
if(GFLAGS_FOUND)
    include_directories(${GFLAGS_INCLUDE_DIR})
    target_link_libraries(folly ${GFLAGS_LIBRARY})
endif(GFLAGS_FOUND)

find_package(GLOG)
if(GLOG_FOUND)
    include_directories(${GLOG_INCLUDE_DIR})
    target_link_libraries(folly ${GLOG_LIBRARY})
endif(GLOG_FOUND)

find_package(DOUBLECONVERSION)
if(DOUBLECONVERSION_FOUND)
    include_directories(${DOUBLECONVERSION_INCLUDE_DIR})
    target_link_libraries(folly ${DOUBLECONVERSION_LIBRARY})
endif(DOUBLECONVERSION_FOUND)


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
