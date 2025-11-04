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
  include(FBPythonWrapper)
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
  find_package(Cython REQUIRED)

  # Create tree of symbolic links in structure required for successful
  # compliation by Cython.
  #   - must be in path named same as extension

  set(_cybld "${CMAKE_CURRENT_BINARY_DIR}/cybld")

  ########################################################################
  # create_source_symlink - map source files into cython build directory
  ########################################################################
  add_custom_target(create_source_symlink ALL)
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
    add_custom_command(TARGET create_source_symlink
      COMMAND
        ${CMAKE_COMMAND} -E create_symlink
        "${_src}"
        "${_cybld}/folly/${_target_file}"
    )
  endforeach()

  ########################################################################
  # folly_python_binding_headers - headers for C++ -> Python bindings
  ########################################################################
  add_custom_target(
    folly_python_binding_headers ALL
    # DEPENDS
    #   create_binding_symlink_python_types
    #   create_binding_symlink_python
    #   create_binding_symlink_py3
    #   thriftcpp2
    WORKING_DIRECTORY ${_cybld}
  )
  add_dependencies(folly_python_binding_headers create_source_symlink)

  add_custom_command(TARGET folly_python_binding_headers
    COMMAND ${CMAKE_COMMAND} -E env
      "CFLAGS=${CMAKE_C_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CXXFLAGS=${CMAKE_CXX_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CXX=${CMAKE_CXX_COMPILER}"
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py -v --api-only
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      ${CMAKE_CURRENT_SOURCE_DIR}/python/executor.pxd
      ${CMAKE_CURRENT_SOURCE_DIR}/python/executor_detail.pxd
      ${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf.pxd
      ${CMAKE_CURRENT_SOURCE_DIR}/python/executor.pyx
      ${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf.pyx
    BYPRODUCTS
      ${_cybld}/folly/iobuf_api.h
    WORKING_DIRECTORY ${_cybld}
  )


  ########################################################################
  # create_binding_symlink - create a well-ordered symlink structure
  ########################################################################
  add_custom_target(
    create_binding_symlink ALL
    DEPENDS folly_python_binding_headers)

  add_custom_command(TARGET create_binding_symlink
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink
      "${_cybld}/folly/iobuf_api.h"
      "${_cybld}/folly/python/iobuf_api.h"
  )

  install(
    FILES
      ${_cybld}/folly/iobuf_api.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/folly/python
    COMPONENT dev
  )


  ########################################################################
  # folly_python_cpp - the C++ implementation of the Python bindings
  ########################################################################
  add_library(
    folly_python_cpp
      python/AsyncioExecutor.cpp
      python/error.cpp
      python/executor.cpp
      python/iobuf.cpp
  )
  add_dependencies(folly_python_cpp folly_python_binding_headers create_binding_symlink)
  set_property(TARGET folly_python_cpp PROPERTY VERSION ${PACKAGE_VERSION})

  target_compile_definitions(folly_python_cpp PRIVATE BOOST_NO_AUTO_PTR)
  target_include_directories(folly_python_cpp PRIVATE "${_cybld}")
  target_include_directories(folly_python_cpp PRIVATE ${Python3_INCLUDE_DIRS})
  target_link_libraries(folly_python_cpp PRIVATE Python3::Python)
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


  ########################################################################
  # folly_python_bindings - cython implementation of py -> c++ bindings
  ########################################################################

  # Tell setup.py where to find includes and libfolly.so
  set(prop "$<TARGET_PROPERTY:folly_base,INCLUDE_DIRECTORIES>")
  set(incs "$<$<BOOL:${prop}>:-I$<JOIN:${prop},:>>")
  set(glog_lib "${GLOG_LIBRARY}")
  cmake_path(REMOVE_FILENAME glog_lib)
  set(libs "-L${CMAKE_BINARY_DIR}:${CMAKE_BINARY_DIR}/folly:${glog_lib}")

  add_custom_target(folly_python_bindings ALL
    DEPENDS folly create_binding_symlink
    WORKING_DIRECTORY ${_cybld})

  add_custom_command(TARGET folly_python_bindings POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
      "CFLAGS=${CMAKE_C_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CXXFLAGS=${CMAKE_CXX_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CC=${CMAKE_C_COMPILER}"
      "CXX=${CMAKE_CXX_COMPILER}"
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      build_ext -f ${incs} ${libs}
    # BYPRODUCTS
    #   ${_cybld}/folly/iobuf_api.h
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      ${CMAKE_CURRENT_SOURCE_DIR}/python/executor.pyx
      ${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf.pyx
      ${CMAKE_CURRENT_SOURCE_DIR}/python/iobuf_ext.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/python/ProactorExecutor.cpp
      # ${CMAKE_CURRENT_SOURCE_DIR}/python/*.pxd
      # ${CMAKE_CURRENT_SOURCE_DIR}/python/*.h
    WORKING_DIRECTORY ${_cybld}
  )
  add_dependencies(folly_python_bindings folly_python_cpp)

  add_custom_command(TARGET folly_python_bindings
    COMMAND ${CMAKE_COMMAND} -E env
      "CFLAGS=${CMAKE_C_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CXXFLAGS=${CMAKE_CXX_FLAGS} ${FOLLY_CXX_OPTIONS}"
      "CC=${CMAKE_C_COMPILER}"
      "CXX=${CMAKE_CXX_COMPILER}"
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      bdist_wheel
    WORKING_DIRECTORY ${_cybld}
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
  wrap_non_fb_python_library(folly_python_cpp folly)
  install(
    TARGETS folly_python_cpp.py_lib
    EXPORT folly
    DESTINATION ${LIB_INSTALL_DIR}
  )
endif ()
