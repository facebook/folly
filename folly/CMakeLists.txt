# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(
    follybenchmark
    Benchmark.cpp
)
set_property(TARGET follybenchmark PROPERTY VERSION ${PACKAGE_VERSION})
target_compile_definitions(follybenchmark PRIVATE BOOST_NO_AUTO_PTR)
target_link_libraries(follybenchmark PUBLIC folly)
apply_folly_compile_options_to_target(follybenchmark)
install(
  TARGETS follybenchmark
  EXPORT folly
  RUNTIME DESTINATION ${BIN_INSTALL_DIR}
  LIBRARY DESTINATION ${LIB_INSTALL_DIR}
  ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
)

add_subdirectory(debugging/exception_tracer)
add_subdirectory(logging/example)

if (PYTHON_EXTENSIONS)
  # Create tree of symbolic links in structure required for successful
  # compliation by Cython.
  #   - must be in path named same as extension

  set(_cybld "${CMAKE_CURRENT_BINARY_DIR}/cybld")

  add_custom_target(create_binding_symlink ALL)
  file(GLOB BindingFiles
    "${CMAKE_CURRENT_SOURCE_DIR}/python/executor.pyx"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf.pyx"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf_ext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/ProactorExecutor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.h"
  )
  file(MAKE_DIRECTORY "${_cybld}/folly/")
  file(MAKE_DIRECTORY "${_cybld}/folly/python/")

  foreach(_src ${BindingFiles})
    get_filename_component(_target_file "${_src}" NAME)

    message(
      STATUS
      "Linking ${_src} "
      "to ${_cybld}/folly/${_target_file}"
    )
    add_custom_command(TARGET create_binding_symlink PRE_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E create_symlink
        "${_src}"
        "${_cybld}/folly/${_target_file}"
    )
  endforeach()

  # Tell setup.py where to find includes and libfolly.so
  set(prop "$<TARGET_PROPERTY:folly_base,INCLUDE_DIRECTORIES>")
  set(incs "$<$<BOOL:${prop}>:-I$<JOIN:${prop},:>>")
  set(glog_lib "${GLOG_LIBRARY}")
  cmake_path(REMOVE_FILENAME glog_lib)
  set(libs "-L${CMAKE_BINARY_DIR}:${glog_lib}")

  add_custom_target(folly_python_bindings ALL
    DEPENDS folly create_binding_symlink
    WORKING_DIRECTORY ${_cybld})

  add_custom_command(TARGET folly_python_bindings POST_BUILD
    COMMAND
      CFLAGS="${CMAKE_C_FLAGS}" CXXFLAGS="${CMAKE_CXX_FLAGS}"
      CC="${CMAKE_C_COMPILER}" CXX="${CMAKE_CXX_COMPILER}"
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      build_ext -f ${incs} ${libs}
    BYPRODUCTS
      ${_cybld}/folly/executor_api.h
      ${_cybld}/folly/iobuf_api.h
    WORKING_DIRECTORY ${_cybld}
  )

  add_custom_target(create_post_binding_symlink ALL)
  add_custom_command(TARGET create_post_binding_symlink
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink
      "${_cybld}/folly/executor_api.h"
      "${_cybld}/folly/python/executor_api.h"
  )
  add_custom_command(TARGET create_post_binding_symlink
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink
      "${_cybld}/folly/iobuf_api.h"
      "${_cybld}/folly/python/iobuf_api.h"
  )

  add_library(
    folly_python_cpp
      python/error.cpp
      python/executor.cpp
      python/iobuf.cpp
  )

  add_dependencies(folly_python_cpp folly_python_bindings create_post_binding_symlink)
  set_property(TARGET folly_python_cpp PROPERTY VERSION ${PACKAGE_VERSION})
  target_compile_definitions(folly_python_cpp PRIVATE BOOST_NO_AUTO_PTR)
  target_include_directories(folly_python_cpp PRIVATE "${_cybld}")
  target_link_libraries(folly_python_cpp PUBLIC folly)
  apply_folly_compile_options_to_target(folly_python_cpp)
  install(
    TARGETS folly_python_cpp
    EXPORT folly
    DESTINATION ${LIB_INSTALL_DIR}
  )
  install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION ${INCLUDE_INSTALL_DIR}
    FILES_MATCHING
      PATTERN "*.h"
  )

  add_custom_command(TARGET folly_python_bindings
    COMMAND
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      bdist_wheel
    WORKING_DIRECTORY ${_cybld}
  )

  install(
    FILES
      ${_cybld}/folly/executor_api.h
      ${_cybld}/folly/iobuf_api.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/folly/python
    COMPONENT dev
  )

  # Install Folly Python bindings. getdeps.py does not allow sufficient
  # control of CMAKE_INSTALL_PREFIX, so PYTHON_PACKAGE_INSTALL_DIR can
  # be used to control the installation location of Python packages.
  set(INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
  if (NOT ${PYTHON_PACKAGE_INSTALL_DIR} STREQUAL "")
    set(INSTALL_DIR ${PYTHON_PACKAGE_INSTALL_DIR})
  endif()

  install(CODE "
    execute_process(
      COMMAND
        python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py install
        --prefix ${INSTALL_DIR}
      COMMAND_ECHO STDOUT
      WORKING_DIRECTORY ${_cybld}
    )"
  )
endif ()
