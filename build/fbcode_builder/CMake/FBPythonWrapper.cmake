# Copyright (c) Meta Platforms, Inc. and affiliates.

function(_write_manifest MANIFEST_FILE PY_TARGET EGG_NAME)
  set(egg_glob "${CMAKE_INSTALL_PREFIX}/lib/python*/site-packages/${EGG_NAME}-*.egg")

  add_dependencies("${PY_TARGET}" "${MANIFEST_FILE}")
  set("${PY_TARGET}" INTERFACE_INCLUDE_DIRECTORIES ${MANIFEST_FILE})
  set("${PY_TARGET}" INTERFACE_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib")

  set(_install_code [[
    file(GLOB _egg_root @egg_glob@)
    file(GLOB_RECURSE _egg_files
      LIST_DIRECTORIES false
      "${_egg_root}/*"
    )

    set(_manifest_path "@MANIFEST_FILE@")

    set(_manifest_contents "FBPY_MANIFEST 1\n")

    foreach(_absfile IN LISTS _egg_files)
      cmake_path(RELATIVE_PATH _absfile BASE_DIRECTORY ${_egg_root} OUTPUT_VARIABLE _egg_relative)

      cmake_path(RELATIVE_PATH _absfile BASE_DIRECTORY ${CMAKE_INSTALL_PREFIX} OUTPUT_VARIABLE _manifest_relative)
      set("${PY_TARGET}" INTERFACE_SOURCES $_absfile)
      if(NOT ${_egg_relative} MATCHES "^EGG-INFO/")
        string(APPEND _manifest_contents "${_manifest_relative} :: ${_egg_relative}\n")
      endif()
    endforeach()

    file(WRITE ${_manifest_path} ${_manifest_contents})
  ]])
  string(CONFIGURE "${_install_code}" _install_code @ONLY)
  install(CODE "${_install_code}" DESTINATION ${MANIFEST_FILE} EXPORT)
endfunction()

function(wrap_non_fb_python_library TARGET EGG_NAME)
  set(py_lib "${TARGET}.py_lib")
  add_library("${py_lib}" INTERFACE)
  install(TARGETS "${py_lib}" EXPORT)

  set(manifest_filename "${TARGET}.manifest")
  set(build_manifest "${CMAKE_CURRENT_BINARY_DIR}/${manifest_filename}")
  set(install_manifest "${CMAKE_INSTALL_PREFIX}/${manifest_filename}")
  target_include_directories(
    "${py_lib}" INTERFACE
    "$<BUILD_INTERFACE:${build_manifest}>"
    "$<INSTALL_INTERFACE:${install_manifest}>"
  )

  _write_manifest("${install_manifest}" "${py_lib}" "${EGG_NAME}")

  set(build_install_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.lib_install")
  set(build_install_manifest "${build_install_dir}/${manifest_filename}")
  add_custom_command(
    OUTPUT
      "${build_install_manifest}"
    COMMAND false
    DEPENDS
      "${install_manifest}"
  )
  add_custom_target(
    "${TARGET}.py_lib_install"
    DEPENDS "${build_install_manifest}"
  )

  # FIXME: do we need the logic from FBPythonBinary:543-553?
endfunction()
