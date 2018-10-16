
cmake_minimum_required(VERSION 3.7)

project(pushmi-project CXX)

option(PUSHMI_USE_CONCEPTS_EMULATION "Use C++14 Concepts Emulation" ON)
option(PUSHMI_USE_CPP_2A "Use C++2a with concepts emulation" OFF)
option(PUSHMI_USE_CPP_17 "Use C++17 with concepts emulation" OFF)

FIND_PACKAGE (Threads REQUIRED)

add_library(pushmi INTERFACE)

target_include_directories(pushmi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/executors-impl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/futures-impl/future-executor-interaction/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/networking-ts-impl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/single_include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nonius/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/executors>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/futures>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/net>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/Catch2>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/nonius>)


if (PUSHMI_USE_CONCEPTS_EMULATION)

message("Using concepts emulation!")

if(${CMAKE_VERSION} VERSION_LESS "3.8.0" OR PUSHMI_USE_CPP_17 OR PUSHMI_USE_CPP_2A)

if (PUSHMI_USE_CPP_17)
message("Using c++17!")
target_compile_options(pushmi INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-std=c++17>
    )
elseif (PUSHMI_USE_CPP_2A)
message("Using c++2a!")
target_compile_options(pushmi INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-std=c++2a>
    )
else()
message("Using c++14!")
target_compile_options(pushmi INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-std=c++14>
    )
endif()

else()

target_compile_features(pushmi INTERFACE cxx_std_14)

endif()

else(PUSHMI_USE_CONCEPTS_EMULATION)

message("Using real concepts!")
message("Using c++2a!")

target_compile_options(pushmi INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-std=c++2a>
    $<$<CXX_COMPILER_ID:GNU>:-fconcepts>)

endif(PUSHMI_USE_CONCEPTS_EMULATION)


target_compile_options(pushmi INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-ftemplate-backtrace-limit=0>)
if (PUSHMI_CONCEPTS)
    target_compile_options(pushmi INTERFACE $<$<CXX_COMPILER_ID:GNU>:-fconcepts>)
endif ()

add_custom_target(buildSingleHeader
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/buildSingleHeader.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(pushmi buildSingleHeader)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/executors-impl/include/
    DESTINATION include/executors)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/futures-impl/future-executor-interaction/include/
    DESTINATION include/futures)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/networking-ts-impl/include/
    DESTINATION include/net)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/
    DESTINATION include/Catch2)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/nonius/include/
    DESTINATION include/nonius)
install(TARGETS pushmi EXPORT pushmi-project)
install(EXPORT pushmi-project DESTINATION pushmi-project)

add_subdirectory(examples)

add_subdirectory(benchmarks)

enable_testing()
include(CTest)
add_subdirectory(test)
