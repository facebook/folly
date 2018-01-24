add_library(
    follybenchmark
    Benchmark.cpp
)
target_link_libraries(follybenchmark PUBLIC folly)
apply_folly_compile_options_to_target(follybenchmark)
install(TARGETS follybenchmark
  EXPORT folly
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

add_subdirectory(experimental/exception_tracer)
