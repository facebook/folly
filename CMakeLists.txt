cmake_minimum_required(VERSION 2.8)

set(PROJECT_NAME folly)
project(${PROJECT_NAME} C CXX)

# Add our custom cmake modules
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
message(STATUS "Module path: ${CMAKE_MODULE_PATH}")

# options
include(folly/CMake/options.cmake)


include_directories(${CMAKE_SOURCE_DIR})
if(MSVC)
include_directories(${CMAKE_SOURCE_DIR}/folly/win32)
endif()


# libfolly definition
# we could do this with recursion but then we'd have to filter back out tests
# explicit is better

# /folly (without benchmark and timeoutqueue)
file(GLOB Folly_HEADERS folly/*.h)
file(GLOB Folly_SOURCES folly/*.cpp)

list(REMOVE_ITEM Folly_SOURCES
    ${CMAKE_SOURCE_DIR}/folly/Benchmark.cpp
    ${CMAKE_SOURCE_DIR}/folly/TimeoutQueue.cpp
    )
list(REMOVE_ITEM Folly_HEADERS
    ${CMAKE_SOURCE_DIR}/folly/Benchmark.h
    ${CMAKE_SOURCE_DIR}/folly/TimeoutQueue.h
    )

# /folly/detail (without malloc if weak symbols, without futex if not linux,
# without clock if clock gettime, without functionalexcept if funcexcept)
file(GLOB Folly_detail_HEADERS folly/detail/*.h)
file(GLOB Folly_detail_SOURCES folly/detail/*.cpp)

# the library items that use futex are linux only
INCLUDE(CheckIncludeFileCXX)
CHECK_INCLUDE_FILE_CXX("linux/futex.h" HAVE_FUTEX_H)

# TODO: futex usage could possibly be replaced by windows/osx specific features
if(NOT HAVE_FUTEX_H)
    list(REMOVE_ITEM Folly_detail_SOURCES
        ${CMAKE_SOURCE_DIR}/folly/detail/MemoryIdler.cpp
        ${CMAKE_SOURCE_DIR}/folly/detail/Futex.cpp
        )
    list(REMOVE_ITEM Folly_detail_HEADERS
        ${CMAKE_SOURCE_DIR}/folly/detail/MemoryIdler.h
        ${CMAKE_SOURCE_DIR}/folly/detail/Futex.h
        )
endif()

# if the library doesn't have weak symbols, add malloc.cpp
INCLUDE(CheckCSourceCompiles) 
CHECK_C_SOURCE_COMPILES("
    extern \"C\" void configure_link_extern_weak_test() __attribute__((weak));
    int main(int argc, char** argv) {
        return configure_link_extern_weak_test == nullptr;
    }
"
    HAVE_WEAK_SYMBOLS
)

if(HAVE_WEAK_SYMBOLS)
    list(REMOVE_ITEM Folly_detail_SOURCES
        ${CMAKE_SOURCE_DIR}/folly/detail/Malloc.cpp
        )
    list(REMOVE_ITEM Folly_detail_HEADERS
        ${CMAKE_SOURCE_DIR}/folly/detail/Malloc.h
        )
endif()

# exception wrappers
CHECK_INCLUDE_FILE_CXX(bits/funcexcept.h FOLLY_HAVE_BITS_FUNCTEXCEPT_H)
if(FOLLY_HAVE_BITS_FUNCTEXCEPT_H)
    list(REMOVE_ITEM Folly_detail_SOURCES
        ${CMAKE_SOURCE_DIR}/folly/detail/FunctionalExcept.cpp
        )
    list(REMOVE_ITEM Folly_detail_HEADERS
        ${CMAKE_SOURCE_DIR}/folly/detail/FunctionalExcept.h
        )
endif()

CHECK_INCLUDE_FILE_CXX(sys/time.h HAVE_SYS_TIME)
INCLUDE(CheckFunctionExists)
CHECK_FUNCTION_EXISTS(clock_gettime FOLLY_HAVE_CLOCK_GETTIME)
if(FOLLY_HAVE_CLOCK_GETTIME)
    list(REMOVE_ITEM Folly_detail_SOURCES
        ${CMAKE_SOURCE_DIR}/folly/detail/Clock.cpp
        )
    list(REMOVE_ITEM Folly_detail_HEADERS
        ${CMAKE_SOURCE_DIR}/folly/detail/Clock.h
        )
endif()

# /folly/gen
file(GLOB Folly_gen_SOURCES folly/gen/*.cpp)
file(GLOB Folly_gen_HEADERS folly/gen/*.h)

# /folly/io
file(GLOB Folly_io_HEADERS folly/io/*.h)
file(GLOB Folly_io_SOURCES folly/io/*.cpp)
# /folly/io/async
file(GLOB Folly_ioasync_HEADERS folly/io/async/*.h)
file(GLOB Folly_ioasync_SOURCES folly/io/async/*.cpp)

# /folly/stats
file(GLOB Folly_stats_HEADERS folly/stats/*.h)
file(GLOB Folly_stats_SOURCES folly/stats/*.cpp)
# /folly/wangle
file(GLOB Folly_wangle_HEADERS folly/wangle/*.h)
file(GLOB Folly_wangle_SOURCES folly/wangle/*.cpp)

# libfolly library
add_library(libfolly STATIC ${Folly_SOURCES} ${Folly_HEADERS}
                            ${Folly_detail_SOURCES} ${Folly_detail_HEADERS}
                            ${Folly_gen_SOURCES} ${Folly_gen_HEADERS}
                            ${Folly_io_SOURCES} ${Folly_io_HEADERS}
                            ${Folly_ioasync_SOURCES} ${Folly_ioasync_HEADERS}
                            ${Folly_stats_SOURCES} ${Folly_stats_HEADERS}
                            ${Folly_wangle_SOURCES} ${Folly_wangle_HEADERS})

if(enable_libfolly_benchmark)
    # benchmark
    add_library(libfolly_benchmark STATIC folly/Benchmark.cpp folly/Benchmark.h)
    target_link_libraries(libfolly_benchmark libfolly)
endif()

if(enable_libfolly_timeout)
    # timeout
    add_library(libfolly_timeout STATIC folly/TimeoutQueue.cpp folly/TimeoutQueue.h)
    target_link_libraries(libfolly_timeout libfolly)
endif()

if(enable_libfolly_fingerprint)
    # fingerprint
    # TODO: this requires building an exe and linking it against libfolly - and running it to get the .cpp file
    add_library(libfolly_fingerprint STATIC folly/gen/FingerprintTables.cpp)
    target_link_libraries(libfolly_fingerprint libfolly)
endif()

if(enable_libfolly_experimental)
    # experimental library
    file(GLOB Folly_experimental_SOURCES folly/experimental/*.cpp)
    file(GLOB Folly_experimental_HEADERS folly/experimental/*.cpp)
    file(GLOB Folly_experimental_io_SOURCES folly/experimental/io/*.h)
    file(GLOB Folly_experimental_io_HEADERS folly/experimental/io/*.cpp)
    file(GLOB Folly_experimental_symbolizer_SOURCES folly/experimental/symbolizer/*.h)
    file(GLOB Folly_experimental_symbolizer_HEADERS folly/experimental/symbolizer/*.cpp)
    file(GLOB Folly_experimental_exception_SOURCES folly/experimental/exception_tracer/*.h)
    file(GLOB Folly_experimental_exception_HEADERS folly/experimental/exception_tracer/*.cpp)

    add_library(libfolly_experimental STATIC ${Folly_experimental_SOURCES} ${Folly_experimental_HEADERS}
                                ${Folly_experimental_io_SOURCES} ${Folly_experimental_io_HEADERS}
                                ${Folly_experimental_symbolizer_SOURCES} ${Folly_experimental_symbolizer_HEADERS}
                                ${Folly_experimental_exception_SOURCES} ${Folly_experimental_exception_HEADERS})
    target_link_libraries(libfolly_experimental libfolly)
endif()

# compiler
include(folly/CMake/compiler.cmake)

# configure
include(folly/CMake/configure.cmake)

# dependencies
include(folly/CMake/deps.cmake)

# install
SET(CMAKE_RELEASE_POSTFIX "")
SET(CMAKE_DEBUG_POSTFIX "-debug")

install(TARGETS libfolly DESTINATION lib)
install(FILES something.h DESTINATION include)

# Re-generate file options
if(regenerate_sources)

    include(FindPythonInterp)
    if(NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "
        Unable to find Python interpreter required testing. Please install Python
        or specify the PYTHON_EXECUTABLE CMake variable.")
    endif()

    # we use execute process because we're forcing regeneration
    message("${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_format_tables.py")
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_format_tables.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/folly/gen/
        RESULT_VARIABLE out
        OUTPUT_VARIABLE output)
    message("${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_escape_tables.py")
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_escape_tables.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/folly/gen/
        RESULT_VARIABLE out
        OUTPUT_VARIABLE output)
    message("${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_varint_tables.py")
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/folly/build/generate_varint_tables.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/folly/gen/
        RESULT_VARIABLE out
        OUTPUT_VARIABLE output)
endif()

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
