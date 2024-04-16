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

add_subdirectory(experimental/exception_tracer)
add_subdirectory(logging/example)

if (PYTHON_EXTENSIONS)
  # Create tree of symbolic links in structure required for successful
  # compliation by Cython.
  #   - must be in path named same as extension

  set(_cybld "${CMAKE_CURRENT_BINARY_DIR}/cybld")

  add_custom_target(create_binding_symlink ALL)
  file(GLOB BindingFiles
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.pyx"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.pxd"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/*.cpp"
  )
  file(MAKE_DIRECTORY "${_cybld}/folly/")

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
  set(libs "-L${CMAKE_BINARY_DIR}")

  add_custom_target(folly_python_bindings ALL
    DEPENDS folly create_binding_symlink
    WORKING_DIRECTORY ${_cybld})

  add_custom_command(TARGET folly_python_bindings POST_BUILD
    COMMAND
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      build_ext -f ${incs} ${libs}
    BYPRODUCTS ${_cybld}/folly/executor_api.h
    WORKING_DIRECTORY ${_cybld}
  )

  add_custom_command(TARGET folly_python_bindings POST_BUILD
    COMMAND
      python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py
      bdist_wheel
    WORKING_DIRECTORY ${_cybld}
  )

  install(
    FILES ${_cybld}/folly/executor_api.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/folly/python
    COMPONENT dev
  )

  # Install Folly Python Bindings
  if (UNIX)
    set(ROOT_ARG "--root \$DESTDIR/")
  endif ()

  install(CODE "
    execute_process(
      COMMAND
        python3 ${CMAKE_CURRENT_SOURCE_DIR}/python/setup.py install
        --prefix ${CMAKE_INSTALL_PREFIX} ${ROOT_ARG}
      COMMAND_ECHO STDOUT
      WORKING_DIRECTORY ${_cybld}
    )"
  )
endif ()
