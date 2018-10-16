

add_executable(PushmiTest
  catch.cpp
  FlowTest.cpp
  CompileTest.cpp
  NewThreadTest.cpp
  TrampolineTest.cpp
  PushmiTest.cpp
)
target_link_libraries(PushmiTest
  pushmi
  Threads::Threads
)

include(../external/Catch2/contrib/Catch.cmake)
catch_discover_tests(PushmiTest)
