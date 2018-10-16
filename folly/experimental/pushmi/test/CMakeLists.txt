

add_executable(PushmiTest
  catch.cpp
  CompileTest.cpp
  NewThreadTest.cpp
  TrampolineTest.cpp
  PushmiTest.cpp
)
target_link_libraries(PushmiTest
  pushmi
  Threads::Threads
)
