# Copyright (c) Facebook, Inc. and its affiliates.
# NOTE: If you change this file, fbcode/fboss/github/ThriftCppLibrary.cmake also
# needs to be changed.  TODO: this should be handled via shipit.
function(add_thrift_cpp2_library LIB_NAME THRIFT_FILE)
  # Parse the arguments
  set(SERVICES)
  set(DEPENDS)
  set(GEN_ARGS)
  set(mode "UNSET")
  foreach(arg IN LISTS ARGN)
    if("${arg}" STREQUAL "SERVICES")
      set(mode "SERVICES")
    elseif("${arg}" STREQUAL "DEPENDS")
      set(mode "DEPENDS")
    elseif("${arg}" STREQUAL "OPTIONS")
      set(mode "OPTIONS")
    else()
      if("${mode}" STREQUAL "SERVICES")
        list(APPEND SERVICES "${arg}")
      elseif("${mode}" STREQUAL "DEPENDS")
        list(APPEND DEPENDS "${arg}")
      elseif("${mode}" STREQUAL "OPTIONS")
        list(APPEND GEN_ARGS "${arg}")
      else()
        message(
          FATAL_ERROR
          "expected SERVICES, DEPENDS, or OPTIONS argument, found ${arg}"
        )
      endif()
    endif()
  endforeach()

  get_filename_component(base ${THRIFT_FILE} NAME_WE)
  get_filename_component(
    output_dir
    ${CMAKE_CURRENT_BINARY_DIR}/${THRIFT_FILE}
    DIRECTORY
  )

  list(APPEND GEN_ARGS "include_prefix=${output_dir}")
  # CMake 3.12 is finally getting a list(JOIN) function, but until then
  # treating the list as a string and replacing the semicolons is good enough.
  string(REPLACE ";" "," GEN_ARG_STR "${GEN_ARGS}")

  # Compute the list of generated files
  list(APPEND generated_headers
    ${output_dir}/gen-cpp2/${base}_constants.h
    ${output_dir}/gen-cpp2/${base}_constants.cpp
    ${output_dir}/gen-cpp2/${base}_types.h
    ${output_dir}/gen-cpp2/${base}_types.tcc
    ${output_dir}/gen-cpp2/${base}_types_custom_protocol.h
  )
  list(APPEND generated_sources
    ${output_dir}/gen-cpp2/${base}_data.h
    ${output_dir}/gen-cpp2/${base}_data.cpp
    ${output_dir}/gen-cpp2/${base}_types.cpp
  )
  foreach(service IN LISTS SERVICES)
    list(APPEND generated_headers
      ${output_dir}/gen-cpp2/${service}.h
      ${output_dir}/gen-cpp2/${service}.tcc
      ${output_dir}/gen-cpp2/${service}AsyncClient.h
      ${output_dir}/gen-cpp2/${service}_custom_protocol.h
    )
    list(APPEND generated_sources
      ${output_dir}/gen-cpp2/${service}.cpp
      ${output_dir}/gen-cpp2/${service}AsyncClient.cpp
      ${output_dir}/gen-cpp2/${service}_processmap_binary.cpp
      ${output_dir}/gen-cpp2/${service}_processmap_compact.cpp
    )
  endforeach()

  # Emit the rule to run the thrift compiler
  add_custom_command(
    OUTPUT
      ${generated_headers}
      ${generated_sources}
    COMMAND
      ${CMAKE_COMMAND} -E make_directory ${output_dir}
    COMMAND
      ${FBTHRIFT_COMPILER}
      --strict
      --templates ${FBTHRIFT_TEMPLATES_DIR}
      --gen "mstch_cpp2:${GEN_ARG_STR}"
      -I ${CMAKE_SOURCE_DIR}
      -o ${output_dir}
      ${CMAKE_CURRENT_SOURCE_DIR}/${THRIFT_FILE}
    WORKING_DIRECTORY
      ${CMAKE_BINARY_DIR}
    MAIN_DEPENDENCY
      ${THRIFT_FILE}
    DEPENDS
      ${DEPENDS}
  )

  # Now emit the library rule to compile the sources
  add_library(${LIB_NAME} STATIC
    ${generated_sources}
  )
  set_property(
    TARGET ${LIB_NAME}
    PROPERTY PUBLIC_HEADER
      ${generated_headers}
  )
  target_include_directories(
    ${LIB_NAME}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
      ${FOLLY_INCLUDE_DIR}
      ${FBTHRIFT_INCLUDE_DIR}
  )
  target_link_libraries(
    ${LIB_NAME}
    PUBLIC
      ${DEPENDS}
      FBThrift::thriftcpp2
      Folly::folly
  )
endfunction()
