

add_executable(composition_2 composition_2.cpp)
target_link_libraries(composition_2
  pushmi
  examples
  Threads::Threads)
  
add_executable(composition_3 composition_3.cpp)
  target_link_libraries(composition_3
    pushmi
    examples
    Threads::Threads)

add_executable(composition_4 composition_4.cpp)
  target_link_libraries(composition_4
    pushmi
    examples
    Threads::Threads)
  