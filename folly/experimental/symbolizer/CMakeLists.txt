if (FOLLY_USE_SYMBOLIZER)
  file(GLOB files *.cpp)
  add_library(
    folly_symbolizer
    ${files}
  )
  apply_folly_compile_options_to_target(folly_symbolizer)
  target_link_libraries(
    folly_symbolizer
    PUBLIC folly
  )
endif()
