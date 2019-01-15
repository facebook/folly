add_library(
    follybenchmark
    Benchmark.cpp
)
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

if (CYTHON_FOUND AND PYTHONINTERP_FOUND)
  # Create tree of symbolic links in structure required for successful
  # compliation by Cython.
  #   - must be in path named same as extension

  set(_cybld "${CMAKE_CURRENT_BINARY_DIR}/cybld")

  file(MAKE_DIRECTORY "${_cybld}/folly")
  foreach(_src
    "executor.pxd"
    "executor.pyx"
    "futures.pxd"
    "__init__.pxd"
    "AsyncioExecutor.h")
    message(STATUS "Linking ${CMAKE_CURRENT_SOURCE_DIR}/python/${_src} to ${_cybld}/folly/${_src}")
    add_custom_target("create_folly_link_${_src}" ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/python/${_src} "${_cybld}/folly/${_src}")
  endforeach()

  add_custom_target(folly_python_bindings ALL
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/setup.py build_ext -f
    DEPENDS folly_pic
    WORKING_DIRECTORY ${_cybld})

  # Install Folly Python Bindings
  install(CODE "
    string(REGEX REPLACE \"^(..*)$\" \"--root=\\\\1\" PYROOT \"\$ENV{DESTDIR}\")
    execute_process(COMMAND
    python3 ${CMAKE_CURRENT_SOURCE_DIR}/setup.py install \${PYROOT}
    WORKING_DIRECTORY ${_cybld})")
endif ()
