

add_executable(PushmiTest catch.cpp PushmiTest.cpp)
target_link_libraries(PushmiTest
  pushmi
  Threads::Threads)
