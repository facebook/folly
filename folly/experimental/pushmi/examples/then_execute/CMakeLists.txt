

add_executable(then_execute_2 then_execute_2.cpp)
target_link_libraries(then_execute_2
  pushmi
  examples
  Threads::Threads)
  