
cmake_minimum_required(VERSION 3.7)

project(pushmi-project CXX)

FIND_PACKAGE (Threads)

add_library(pushmi INTERFACE)

target_include_directories(pushmi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/meta/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/executors-impl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/futures-impl/future-executor-interaction/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/networking-ts-impl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/single_include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/meta>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/executors>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/futures>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/net>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/Catch2>)
target_compile_features(pushmi INTERFACE cxx_std_14)
target_compile_options(pushmi INTERFACE
    #$<$<CXX_COMPILER_ID:GNU>:-std=c++2a>
    #$<$<CXX_COMPILER_ID:GNU>:-fconcepts>
    $<$<CXX_COMPILER_ID:GNU>:-ftemplate-backtrace-limit=0>)

add_custom_target(buildSingleHeader COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/buildSingleHeader.cmake WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(pushmi buildSingleHeader)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/meta/include/
    DESTINATION include/meta)
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
install(TARGETS pushmi EXPORT pushmi-project)
install(EXPORT pushmi-project DESTINATION pushmi-project)

add_subdirectory(examples)

enable_testing()
include(CTest)
add_subdirectory(test)
