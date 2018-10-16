
add_library(examples INTERFACE)

target_include_directories(examples INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

add_subdirectory(twoway_execute)
add_subdirectory(then_execute)
add_subdirectory(composition)
add_subdirectory(for_each)
add_subdirectory(reduce)
add_subdirectory(set_done)
add_subdirectory(set_error)
