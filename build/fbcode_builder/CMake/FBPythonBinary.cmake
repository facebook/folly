# Copyright (c) Facebook, Inc. and its affiliates.

include(FBCMakeParseArgs)

#
# This file contains helper functions for building self-executing Python
# binaries.
#
# This is somewhat different than typical python installation with
# distutils/pip/virtualenv/etc.  We primarily want to build a standalone
# executable, isolated from other Python packages on the system.  We don't want
# to install files into the standard library python paths.  This is more
# similar to PEX (https://github.com/pantsbuild/pex) and XAR
# (https://github.com/facebookincubator/xar).  (In the future it would be nice
# to update this code to also support directly generating XAR files if XAR is
# available.)
#
# We also want to be able to easily define "libraries" of python files that can
# be shared and re-used between these standalone python executables, and can be
# shared across projects in different repositories.  This means that we do need
# a way to "install" libraries so that they are visible to CMake builds in
# other repositories, without actually installing them in the standard python
# library paths.
#

# Find our helper program.
# We typically install this in the same directory as this .cmake file.
find_program(
  MAKE_PYTHON_ARCHIVE "make_fbpy_archive.py"
  PATHS ${CMAKE_MODULE_PATH}
)
if (NOT MAKE_PYTHON_ARCHIVE)
  message(
    FATAL_ERROR "unable to find make_fbpy_archive.py helper program (it "
    "should be located in the same directory as FBPythonRules.cmake)"
  )
endif()

# An option to control the default installation location for
# install_fb_python_library().  This is relative to ${CMAKE_INSTALL_PREFIX}
set(
  FBPY_LIB_INSTALL_DIR "lib/fb-py-libs" CACHE STRING
  "The subdirectory where FB python libraries should be installed"
)

#
# Build a self-executing python binary.
#
# This accepts the same arguments as add_fb_python_library().
# In addition, a MAIN_MODULE argument is required.  This argument specifies
# which module should be started as the __main__ module when the executable is
# run.
#
function(add_fb_python_executable EXE_NAME)
  # Parse the arguments
  set(one_value_args BASE_DIR NAMESPACE MAIN_MODULE TYPE)
  set(multi_value_args SOURCES DEPENDS)
  fb_cmake_parse_args(
    ARG "" "${one_value_args}" "${multi_value_args}" "${ARGN}"
  )
  fb_py_process_default_args(ARG_NAMESPACE ARG_BASE_DIR)

  # Use add_fb_python_library() to perform most of our source handling
  add_fb_python_library(
    "${EXE_NAME}.main_lib"
    BASE_DIR "${ARG_BASE_DIR}"
    NAMESPACE "${ARG_NAMESPACE}"
    SOURCES ${ARG_SOURCES}
    DEPENDS ${ARG_DEPENDS}
  )

  set(
    manifest_files
    "$<TARGET_PROPERTY:${EXE_NAME}.main_lib.py_lib,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  set(
    source_files
    "$<TARGET_PROPERTY:${EXE_NAME}.main_lib.py_lib,INTERFACE_SOURCES>"
  )

  # The command to build the executable archive.
  #
  # If we are using CMake 3.8+ we can use COMMAND_EXPAND_LISTS.
  # CMP0067 isn't really the policy we care about, but seems like the best way
  # to check if we are running 3.8+.
  if (POLICY CMP0067)
    set(extra_cmd_params COMMAND_EXPAND_LISTS)
    set(make_py_args "${manifest_files}")
  else()
    set(extra_cmd_params)
    set(make_py_args --manifest-separator "::" "$<JOIN:${manifest_files},::>")
  endif()

  set(output_file "${EXE_NAME}")
  if(DEFINED ARG_TYPE)
    list(APPEND make_py_args "--type" "${ARG_TYPE}")
    if ("${ARG_TYPE}" STREQUAL "dir")
      # CMake doesn't really seem to like having a directory specified as an
      # output; specify the __main__.py file as the output instead.
      set(output_file "${EXE_NAME}/__main__.py")
      list(APPEND
        extra_cmd_params
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${EXE_NAME}"
      )
    endif()
  endif()

  add_custom_command(
    OUTPUT "${output_file}"
    ${extra_cmd_params}
    COMMAND
      "${MAKE_PYTHON_ARCHIVE}" -o "${EXE_NAME}" --main "${ARG_MAIN_MODULE}"
      ${make_py_args}
    DEPENDS
      ${source_files}
      "${EXE_NAME}.main_lib.py_sources_built"
      "${MAKE_PYTHON_ARCHIVE}"
  )

  # Add an "ALL" target that depends on force ${EXE_NAME},
  # so that ${EXE_NAME} will be included in the default list of build targets.
  add_custom_target("${EXE_NAME}.GEN_PY_EXE" ALL DEPENDS "${output_file}")
endfunction()

#
# Define a python library.
#
# If you want to install a python library generated from this rule note that
# you need to use install_fb_python_library() rather than CMake's built-in
# install() function.  This will make it available for other downstream
# projects to use in their add_fb_python_executable() and
# add_fb_python_library() calls.  (You do still need to use `install(EXPORT)`
# later to install the CMake exports.)
#
# Parameters:
# - BASE_DIR <dir>:
#   The base directory path to strip off from each source path.  All source
#   files must be inside this directory.  If not specified it defaults to
#   ${CMAKE_CURRENT_SOURCE_DIR}.
# - NAMESPACE <namespace>:
#   The destination namespace where these files should be installed in python
#   binaries.  If not specified, this defaults to the current relative path of
#   ${CMAKE_CURRENT_SOURCE_DIR} inside ${CMAKE_SOURCE_DIR}.  e.g., a python
#   library defined in the directory repo_root/foo/bar will use a default
#   namespace of "foo.bar"
# - SOURCES <src1> <...>:
#   The python source files.
# - DEPENDS <target1> <...>:
#   Other python libraries that this one depends on.
# - INSTALL_DIR <dir>:
#   The directory where this library should be installed.
#   install_fb_python_library() must still be called later to perform the
#   installation.  If a relative path is given it will be treated relative to
#   ${CMAKE_INSTALL_PREFIX}
#
# CMake is unfortunately pretty crappy at being able to define custom build
# rules & behaviors.  It doesn't support transitive property propagation
# between custom targets; only the built-in add_executable() and add_library()
# targets support transitive properties.
#
# We hack around this janky CMake behavior by (ab)using interface libraries to
# propagate some of the data we want between targets, without actually
# generating a C library.
#
# add_fb_python_library(SOMELIB) generates the following things:
# - An INTERFACE library rule named SOMELIB.py_lib which tracks some
#   information about transitive dependencies:
#   - the transitive set of source files in the INTERFACE_SOURCES property
#   - the transitive set of manifest files that this library depends on in
#     the INTERFACE_INCLUDE_DIRECTORIES property.
# - A custom command that generates a SOMELIB.manifest file.
#   This file contains the mapping of source files to desired destination
#   locations in executables that depend on this library.  This manifest file
#   will then be read at build-time in order to build executables.
#
function(add_fb_python_library LIB_NAME)
  # Parse the arguments
  # We use fb_cmake_parse_args() rather than cmake_parse_arguments() since
  # cmake_parse_arguments() does not handle empty arguments, and it is common
  # for callers to want to specify an empty NAMESPACE parameter.
  set(one_value_args BASE_DIR NAMESPACE INSTALL_DIR)
  set(multi_value_args SOURCES DEPENDS)
  fb_cmake_parse_args(
    ARG "" "${one_value_args}" "${multi_value_args}" "${ARGN}"
  )
  fb_py_process_default_args(ARG_NAMESPACE ARG_BASE_DIR)

  string(REPLACE "." "/" namespace_dir "${ARG_NAMESPACE}")
  if (NOT "${namespace_dir}" STREQUAL "")
    set(namespace_dir "${namespace_dir}/")
  endif()

  if(NOT DEFINED ARG_INSTALL_DIR)
    set(install_dir "${FBPY_LIB_INSTALL_DIR}/")
  elseif("${ARG_INSTALL_DIR}" STREQUAL "")
    set(install_dir "")
  else()
    set(install_dir "${ARG_INSTALL_DIR}/")
  endif()

  # message(STATUS "fb py library ${LIB_NAME}: "
  #         "NS=${namespace_dir} BASE=${ARG_BASE_DIR}")

  # TODO: In the future it would be nice to support pre-compiling the source
  # files.  We could emit a rule to compile each source file and emit a
  # .pyc/.pyo file here, and then have the manifest reference the pyc/pyo
  # files.

  # Define a library target to help pass around information about the library,
  # and propagate dependency information.
  #
  # CMake make a lot of assumptions that libraries are C++ libraries.  To help
  # avoid confusion we name our target "${LIB_NAME}.py_lib" rather than just
  # "${LIB_NAME}".  This helps avoid confusion if callers try to use
  # "${LIB_NAME}" on their own as a target name.  (e.g., attempting to install
  # it directly with install(TARGETS) won't work.  Callers must use
  # install_fb_python_library() instead.)
  add_library("${LIB_NAME}.py_lib" INTERFACE)

  # Emit the manifest file.
  #
  # We write the manifest file to a temporary path first, then copy it with
  # configure_file(COPYONLY).  This is necessary to get CMake to understand
  # that "${manifest_path}" is generated by the CMake configure phase,
  # and allow using it as a dependency for add_custom_command().
  # (https://gitlab.kitware.com/cmake/cmake/issues/16367)
  set(manifest_path "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}.manifest") 
  set(tmp_manifest "${manifest_path}.tmp")
  file(WRITE "${tmp_manifest}" "FBPY_MANIFEST 1\n")
  set(abs_sources)
  foreach(src_path IN LISTS ARG_SOURCES)
    get_filename_component(abs_source "${src_path}" ABSOLUTE)
    list(APPEND abs_sources "${abs_source}")
    file(RELATIVE_PATH rel_src "${ARG_BASE_DIR}" "${abs_source}")
    target_sources(
      "${LIB_NAME}.py_lib" INTERFACE
      "$<BUILD_INTERFACE:${abs_source}>"
      "$<INSTALL_INTERFACE:${install_dir}${LIB_NAME}/${namespace_dir}${rel_src}>"
    )
    if("${rel_src}" MATCHES "^../")
      message(
        FATAL_ERROR "${LIB_NAME}: source file \"${abs_source}\" is not inside "
        "the base directory ${ARG_BASE_DIR}"
      )
    endif()
    file(
      APPEND "${tmp_manifest}"
      "${abs_source} :: ${namespace_dir}${rel_src}\n"
    )
  endforeach()
  configure_file("${tmp_manifest}" "${manifest_path}" COPYONLY)

  target_include_directories(
    "${LIB_NAME}.py_lib" INTERFACE
    "$<BUILD_INTERFACE:${manifest_path}>"
    "$<INSTALL_INTERFACE:${install_dir}${LIB_NAME}.manifest>"
  )

  # Add a target that depends on all of the source files.
  # This is needed in case some of the source files are generated.  This will
  # ensure that these source files are brought up-to-date before we build
  # any python binaries that depend on this library.
  add_custom_target("${LIB_NAME}.py_sources_built" DEPENDS ${ARG_SOURCES})
  add_dependencies("${LIB_NAME}.py_lib" "${LIB_NAME}.py_sources_built")

  # Hook up library dependencies, and also make the *.py_sources_built target
  # depend on the sources for all of our dependencies also being up-to-date.
  foreach(dep IN LISTS ARG_DEPENDS)
    target_link_libraries("${LIB_NAME}.py_lib" INTERFACE "${dep}.py_lib")

    # Mark that our .py_sources_built target depends on each our our dependent
    # libraries.  This serves two functions:
    # - This causes CMake to generate an error message if one of the
    #   dependencies is never defined.  The target_link_libraries() call above
    #   won't complain if one of the dependencies doesn't exist (since it is
    #   intended to allow passing in file names for plain library files rather
    #   than just targets).
    # - It ensures that sources for our depencencies are built before any
    #   executable that depends on us.  Note that we depend on "${dep}.py_lib"
    #   rather than "${dep}.py_sources_built" for this purpose because the
    #   ".py_sources_built" target won't be available for imported targets.
    add_dependencies("${LIB_NAME}.py_sources_built" "${dep}.py_lib")
  endforeach()

  # Add a custom command to help with library installation, in case
  # install_fb_python_library() is called later for this library.
  # add_custom_command() only works with file dependencies defined in the same
  # CMakeLists.txt file, so we want to make sure this is defined here, rather
  # then where install_fb_python_library() is called.
  # This command won't be run by default, but will only be run if it is needed
  # by a subsequent install_fb_python_library() call.
  #
  # This command copies the library contents into the build directory.
  # It would be nicer if we could skip this intermediate copy, and just run
  # make_fbpy_archive.py at install time to copy them directly to the desired
  # installation directory.  Unfortunately this is difficult to do, and seems
  # to interfere with some of the CMake code that wants to generate a manifest
  # of installed files.
  set(build_install_dir "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}.lib_install")
  add_custom_command(
    OUTPUT
      "${build_install_dir}/${LIB_NAME}.manifest"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${build_install_dir}"
    COMMAND
      "${MAKE_PYTHON_ARCHIVE}" --type lib-install
      --install-dir "${LIB_NAME}"
      -o "${build_install_dir}/${LIB_NAME}" "${manifest_path}"
    DEPENDS
      "${ARG_SOURCES}"
      "${manifest_path}"
      "${MAKE_PYTHON_ARCHIVE}"
  )
  add_custom_target(
    "${LIB_NAME}.py_lib_install"
    DEPENDS "${build_install_dir}/${LIB_NAME}.manifest"
  )

  # Set some properties to pass through the install paths to
  # install_fb_python_library()
  #
  # Passing through ${build_install_dir} allows install_fb_python_library()
  # to work even if used from a different CMakeLists.txt file than where
  # add_fb_python_library() was called (i.e. such that
  # ${CMAKE_CURRENT_BINARY_DIR} is different between the two calls).
  set(abs_install_dir "${install_dir}")
  if(NOT IS_ABSOLUTE "${abs_install_dir}")
    set(abs_install_dir "${CMAKE_INSTALL_PREFIX}/${abs_install_dir}")
  endif()
  string(REGEX REPLACE "/$" "" abs_install_dir "${abs_install_dir}")
  set_target_properties(
    "${LIB_NAME}.py_lib_install"
    PROPERTIES
    INSTALL_DIR "${abs_install_dir}"
    BUILD_INSTALL_DIR "${build_install_dir}"
  )
endfunction()

#
# Install a python library.
#
# - EXPORT <export-name>:
#   Associate the installed target files with the given export-name.
#
# Note that unlike the built-in CMake install() function we do not accept a
# DESTINATION parameter.  Instead, use the INSTALL_DIR parameter to
# add_fb_python_library() to set the installation location.
#
function(install_fb_python_library LIB_NAME)
  set(one_value_args EXPORT)
  fb_cmake_parse_args(ARG "" "${one_value_args}" "" "${ARGN}")

  # Export our "${LIB_NAME}.py_lib" target so that it will be available to
  # downstream projects in our installed CMake config files.
  if(DEFINED ARG_EXPORT)
    install(TARGETS "${LIB_NAME}.py_lib" EXPORT "${ARG_EXPORT}")
  endif()

  # add_fb_python_library() emits a .py_lib_install target that will prepare
  # the installation directory.  However, it isn't part of the "ALL" target and
  # therefore isn't built by default.
  #
  # Make sure the ALL target depends on it now.  We have to do this by
  # introducing yet another custom target.
  # Add it as a dependency to the ALL target now.
  add_custom_target("${LIB_NAME}.py_lib_install_all" ALL)
  add_dependencies(
    "${LIB_NAME}.py_lib_install_all" "${LIB_NAME}.py_lib_install"
  )

  # Copy the intermediate install directory generated at build time into
  # the desired install location.
  set(dest_dir "$<TARGET_PROPERTY:${LIB_NAME}.py_lib_install,INSTALL_DIR>")
  set(
    build_install_dir
    "$<TARGET_PROPERTY:${LIB_NAME}.py_lib_install,BUILD_INSTALL_DIR>"
  )
  install(
    DIRECTORY "${build_install_dir}/${LIB_NAME}"
    DESTINATION "${dest_dir}"
  )
  install(
    FILES "${build_install_dir}/${LIB_NAME}.manifest"
    DESTINATION "${dest_dir}"
  )
endfunction()

# Helper macro to process the BASE_DIR and NAMESPACE arguments for
# add_fb_python_executable() and add_fb_python_executable()
macro(fb_py_process_default_args NAMESPACE_VAR BASE_DIR_VAR)
  # If the namespace was not specified, default to the relative path to the
  # current directory (starting from the repository root).
  if(NOT DEFINED "${NAMESPACE_VAR}")
    file(
      RELATIVE_PATH "${NAMESPACE_VAR}"
      "${CMAKE_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()

  if(NOT DEFINED "${BASE_DIR_VAR}")
    # If the base directory was not specified, default to the current directory
    set("${BASE_DIR_VAR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  else()
    # If the base directory was specified, always convert it to an
    # absolute path.
    get_filename_component("${BASE_DIR_VAR}" "${${BASE_DIR_VAR}}" ABSOLUTE)
  endif()
endmacro()
