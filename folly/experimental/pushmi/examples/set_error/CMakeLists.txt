

add_executable(set_error_2 set_error_2.cpp)
target_link_libraries(set_error_2
  pushmi
  examples
  Threads::Threads)
