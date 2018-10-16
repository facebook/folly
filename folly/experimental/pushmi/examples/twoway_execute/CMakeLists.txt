

add_executable(twoway_execute_2 twoway_execute_2.cpp)
target_link_libraries(twoway_execute_2
  pushmi
  examples
  Threads::Threads)
  