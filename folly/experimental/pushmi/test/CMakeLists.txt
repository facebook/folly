add_library(catch STATIC catch.cpp)
target_include_directories(catch PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../external/Catch2/single_include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/Catch2>)

include(../external/Catch2/contrib/Catch.cmake)


if(NOT ${CMAKE_VERSION} VERSION_LESS "3.10.0")

add_executable(FlowTest FlowTest.cpp)
target_link_libraries(FlowTest pushmi catch Threads::Threads)
catch_discover_tests(FlowTest)

add_executable(FlowManyTest FlowManyTest.cpp)
target_link_libraries(FlowManyTest pushmi catch Threads::Threads)
catch_discover_tests(FlowManyTest)

add_executable(CompileTest CompileTest.cpp)
target_link_libraries(CompileTest pushmi catch Threads::Threads)
# catch_discover_tests(CompileTest)

add_executable(NewThreadTest NewThreadTest.cpp)
target_link_libraries(NewThreadTest pushmi catch Threads::Threads)
catch_discover_tests(NewThreadTest)

add_executable(TrampolineTest TrampolineTest.cpp)
target_link_libraries(TrampolineTest pushmi catch Threads::Threads)
catch_discover_tests(TrampolineTest)

add_executable(PushmiTest PushmiTest.cpp)
target_link_libraries(PushmiTest pushmi catch Threads::Threads)
catch_discover_tests(PushmiTest)

else()

add_executable(PushmiTest PushmiTest.cpp
  CompileTest.cpp
  TrampolineTest.cpp
  NewThreadTest.cpp
  FlowTest.cpp
  FlowManyTest.cpp)

target_link_libraries(PushmiTest pushmi catch Threads::Threads)
catch_discover_tests(PushmiTest)

endif()
