

FIND_PACKAGE (Boost)

if (Boost_FOUND)

add_executable(PushmiBenchmarks
  runner.cpp
  PushmiBenchmarks.cpp
)
target_include_directories(PushmiBenchmarks
    PUBLIC ${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../examples/include
)
target_link_libraries(PushmiBenchmarks
  pushmi
  Threads::Threads
)

else()

message(WARNING "Boost not found, no benchmarks will be built")

endif()
