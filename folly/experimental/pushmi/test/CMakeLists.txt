include(../external/Catch2/contrib/Catch.cmake)

add_library(CatchImpl catch.cpp)
target_link_libraries(CatchImpl pushmi)

add_executable(FlowTest FlowTest.cpp)
target_link_libraries(FlowTest pushmi CatchImpl Threads::Threads)
catch_discover_tests(FlowTest)

add_executable(CompileTest CompileTest.cpp)
target_link_libraries(CompileTest pushmi CatchImpl Threads::Threads)
# catch_discover_tests(CompileTest)

add_executable(NewThreadTest NewThreadTest.cpp)
target_link_libraries(NewThreadTest pushmi CatchImpl Threads::Threads)
catch_discover_tests(NewThreadTest)

add_executable(TrampolineTest TrampolineTest.cpp)
target_link_libraries(TrampolineTest pushmi CatchImpl Threads::Threads)
catch_discover_tests(TrampolineTest)

add_executable(PushmiTest PushmiTest.cpp)
target_link_libraries(PushmiTest pushmi CatchImpl Threads::Threads)
catch_discover_tests(PushmiTest)
