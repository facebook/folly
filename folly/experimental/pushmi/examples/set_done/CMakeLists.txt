

add_executable(set_done_2 set_done_2.cpp)
target_link_libraries(set_done_2
  pushmi
  examples
  Threads::Threads)
