

add_executable(reduce_2 reduce_2.cpp)
target_link_libraries(reduce_2
  pushmi
  examples
  Threads::Threads)

add_executable(reduce_3 reduce_3.cpp)
target_link_libraries(reduce_3
  pushmi
  examples
  Threads::Threads)
  