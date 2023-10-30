# Copyright (c) Meta Platforms, Inc. and affiliates.

include(FBCMakeParseArgs)

set(
  USE_CARGO_VENDOR AUTO CACHE STRING
  "Download Rust Crates from an internally vendored location"
)
set_property(CACHE USE_CARGO_VENDOR PROPERTY STRINGS AUTO ON OFF)

set(
  GENERATE_CARGO_VENDOR_CONFIG AUTO CACHE STRING
  "Whether to generate Rust cargo vendor config or use existing"
)
set_property(CACHE GENERATE_CARGO_VENDOR_CONFIG PROPERTY STRINGS AUTO ON OFF)

set(RUST_VENDORED_CRATES_DIR "$ENV{RUST_VENDORED_CRATES_DIR}")

if("${USE_CARGO_VENDOR}" STREQUAL "AUTO")
  if(EXISTS "${RUST_VENDORED_CRATES_DIR}")
    set(USE_CARGO_VENDOR ON)
  else()
    set(USE_CARGO_VENDOR OFF)
  endif()
endif()

if("${GENERATE_CARGO_VENDOR_CONFIG}" STREQUAL "AUTO")
  set(GENERATE_CARGO_VENDOR_CONFIG "${USE_CARGO_VENDOR}")
endif()

if(GENERATE_CARGO_VENDOR_CONFIG)
  if(NOT EXISTS "${RUST_VENDORED_CRATES_DIR}")
    message(
      FATAL "vendored rust crates not present: "
      "${RUST_VENDORED_CRATES_DIR}"
    )
  endif()

  set(RUST_CARGO_HOME "${CMAKE_BINARY_DIR}/_cargo_home")
  file(MAKE_DIRECTORY "${RUST_CARGO_HOME}")

  file(
    TO_NATIVE_PATH "${RUST_VENDORED_CRATES_DIR}"
    ESCAPED_RUST_VENDORED_CRATES_DIR
  )
  string(
    REPLACE "\\" "\\\\"
    ESCAPED_RUST_VENDORED_CRATES_DIR
    "${ESCAPED_RUST_VENDORED_CRATES_DIR}"
  )
  file(
    WRITE "${RUST_CARGO_HOME}/config"
    "[source.crates-io]\n"
    "replace-with = \"vendored-sources\"\n"
    "\n"
    "[source.vendored-sources]\n"
    "directory = \"${ESCAPED_RUST_VENDORED_CRATES_DIR}\"\n"
  )
endif()

find_program(CARGO_COMMAND cargo REQUIRED)

# Cargo is a build system in itself, and thus will try to take advantage of all
# the cores on the system. Unfortunately, this conflicts with Ninja, since it
# also tries to utilize all the cores. This can lead to a system that is
# completely overloaded with compile jobs to the point where nothing else can
# be achieved on the system.
#
# Let's inform Ninja of this fact so it won't try to spawn other jobs while
# Rust being compiled.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS rust_job_pool=1)

# This function creates an interface library target based on the static library
# built by Cargo. It will call Cargo to build a staticlib and generate a CMake
# interface library with it.
#
# This function requires `find_package(Python COMPONENTS Interpreter)`.
#
# You need to set `lib:crate-type = ["staticlib"]` in your Cargo.toml to make
# Cargo build static library.
#
# ```cmake
# rust_static_library(<TARGET> [CRATE <CRATE_NAME>] [FEATURES <FEATURE_NAME>] [USE_CXX_INCLUDE])
# ```
#
# Parameters:
# - TARGET:
#   Name of the target name. This function will create an interface library
#   target with this name.
# - CRATE_NAME:
#   Name of the crate. This parameter is optional. If unspecified, it will
#   fallback to `${TARGET}`.
# - FEATURE_NAME:
#   Name of the Rust feature to enable.
# - USE_CXX_INCLUDE:
#   Include cxx.rs include path in `${TARGET}` INTERFACE.
#
# This function creates two targets:
# - "${TARGET}": an interface library target contains the static library built
#   from Cargo.
# - "${TARGET}.cargo": an internal custom target that invokes Cargo.
#
# If you are going to use this static library from C/C++, you will need to
# write header files for the library (or generate with cbindgen) and bind these
# headers with the interface library.
#
function(rust_static_library TARGET)
  fb_cmake_parse_args(ARG "USE_CXX_INCLUDE" "CRATE;FEATURES" "" "${ARGN}")

  if(DEFINED ARG_CRATE)
    set(crate_name "${ARG_CRATE}")
  else()
    set(crate_name "${TARGET}")
  endif()
  if(DEFINED ARG_FEATURES)
    set(features --features ${ARG_FEATURES})
  else()
    set(features )
  endif()

  set(cargo_target "${TARGET}.cargo")
  set(target_dir $<IF:$<CONFIG:Debug>,debug,release>)
  set(staticlib_name "${CMAKE_STATIC_LIBRARY_PREFIX}${crate_name}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(rust_staticlib "${CMAKE_CURRENT_BINARY_DIR}/${target_dir}/${staticlib_name}")

  if(DEFINED ARG_FEATURES)
    set(cargo_flags build $<IF:$<CONFIG:Debug>,,--release> -p ${crate_name} --features ${ARG_FEATURES})
  else()
    set(cargo_flags build $<IF:$<CONFIG:Debug>,,--release> -p ${crate_name})
  endif()
  if(USE_CARGO_VENDOR)
    set(extra_cargo_env "CARGO_HOME=${RUST_CARGO_HOME}")
    set(cargo_flags ${cargo_flags})
  endif()

  add_custom_target(
    ${cargo_target}
    COMMAND
      "${CMAKE_COMMAND}" -E remove -f "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      ${extra_cargo_env}
      ${CARGO_COMMAND}
      ${cargo_flags}
    COMMENT "Building Rust crate '${crate_name}'..."
    JOB_POOL rust_job_pool
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    BYPRODUCTS
      "${CMAKE_CURRENT_BINARY_DIR}/debug/${staticlib_name}"
      "${CMAKE_CURRENT_BINARY_DIR}/release/${staticlib_name}"
  )

  add_library(${TARGET} INTERFACE)
  add_dependencies(${TARGET} ${cargo_target})
  set_target_properties(
    ${TARGET}
    PROPERTIES
      INTERFACE_STATICLIB_OUTPUT_PATH "${rust_staticlib}"
      INTERFACE_INSTALL_LIBNAME
        "${CMAKE_STATIC_LIBRARY_PREFIX}${crate_name}_rs${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )

  if(DEFINED ARG_USE_CXX_INCLUDE)
    target_include_directories(
      ${TARGET}
      INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/cxxbridge/
    )
  endif()

  target_link_libraries(
    ${TARGET}
    INTERFACE "$<BUILD_INTERFACE:${rust_staticlib}>"
  )
endfunction()

# This function instructs CMake to define a target that will use `cargo build`
# to build a bin crate referenced by the Cargo.toml file in the current source
# directory.
# It accepts a single `TARGET` parameter which will be passed as the package
# name to `cargo build -p TARGET`. If binary has different name as package,
# use optional flag BINARY_NAME to override it.
# It also accepts a `FEATURES` parameter if you want to enable certain features
# in your Rust binary.
# The CMake target will be registered to build by default as part of the
# ALL target.
function(rust_executable TARGET)
  fb_cmake_parse_args(ARG "" "BINARY_NAME;FEATURES" "" "${ARGN}")

  set(crate_name "${TARGET}")
  set(cargo_target "${TARGET}.cargo")
  set(target_dir $<IF:$<CONFIG:Debug>,debug,release>)

  if(DEFINED ARG_BINARY_NAME)
    set(executable_name "${ARG_BINARY_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
  else()
    set(executable_name "${crate_name}${CMAKE_EXECUTABLE_SUFFIX}")
  endif()
  if(DEFINED ARG_FEATURES)
    set(features --features ${ARG_FEATURES})
  else()
    set(features )
  endif()

  if(DEFINED ARG_FEATURES)
    set(cargo_flags build $<IF:$<CONFIG:Debug>,,--release> -p ${crate_name} --features ${ARG_FEATURES})
  else()
    set(cargo_flags build $<IF:$<CONFIG:Debug>,,--release> -p ${crate_name})
  endif()
  if(USE_CARGO_VENDOR)
    set(extra_cargo_env "CARGO_HOME=${RUST_CARGO_HOME}")
    set(cargo_flags ${cargo_flags})
  endif()

  add_custom_target(
    ${cargo_target}
    ALL
    COMMAND
      "${CMAKE_COMMAND}" -E remove -f "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      ${extra_cargo_env}
      ${CARGO_COMMAND}
      ${cargo_flags}
    COMMENT "Building Rust executable '${crate_name}'..."
    JOB_POOL rust_job_pool
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    BYPRODUCTS
      "${CMAKE_CURRENT_BINARY_DIR}/debug/${executable_name}"
      "${CMAKE_CURRENT_BINARY_DIR}/release/${executable_name}"
  )

  set_property(TARGET "${cargo_target}"
      PROPERTY EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/${target_dir}/${executable_name}")
endfunction()

# This function can be used to install the executable generated by a prior
# call to the `rust_executable` function.
# It requires a `TARGET` parameter to identify the target to be installed,
# and an optional `DESTINATION` parameter to specify the installation
# directory.  If DESTINATION is not specified then the `bin` directory
# will be assumed.
function(install_rust_executable TARGET)
  # Parse the arguments
  set(one_value_args DESTINATION)
  set(multi_value_args)
  fb_cmake_parse_args(
    ARG "" "${one_value_args}" "${multi_value_args}" "${ARGN}"
  )

  if(NOT DEFINED ARG_DESTINATION)
    set(ARG_DESTINATION bin)
  endif()

  get_target_property(foo "${TARGET}.cargo" EXECUTABLE)

  install(
    PROGRAMS "${foo}"
    DESTINATION "${ARG_DESTINATION}"
  )
endfunction()

# This function installs the interface target generated from the function
# `rust_static_library`. Use this function if you want to export your Rust
# target to external CMake targets.
#
# ```cmake
# install_rust_static_library(
#   <TARGET>
#   INSTALL_DIR <INSTALL_DIR>
#   [EXPORT <EXPORT_NAME>]
# )
# ```
#
# Parameters:
# - TARGET: Name of the Rust static library target.
# - EXPORT_NAME: Name of the exported target.
# - INSTALL_DIR: Path to the directory where this library will be installed.
#
function(install_rust_static_library TARGET)
  fb_cmake_parse_args(ARG "" "EXPORT;INSTALL_DIR" "" "${ARGN}")

  get_property(
    staticlib_output_path
    TARGET "${TARGET}"
    PROPERTY INTERFACE_STATICLIB_OUTPUT_PATH
  )
  get_property(
    staticlib_output_name
    TARGET "${TARGET}"
    PROPERTY INTERFACE_INSTALL_LIBNAME
  )

  if(NOT DEFINED staticlib_output_path)
    message(FATAL_ERROR "Not a rust_static_library target.")
  endif()

  if(NOT DEFINED ARG_INSTALL_DIR)
    message(FATAL_ERROR "Missing required argument.")
  endif()

  if(DEFINED ARG_EXPORT)
    set(install_export_args EXPORT "${ARG_EXPORT}")
  endif()

  set(install_interface_dir "${ARG_INSTALL_DIR}")
  if(NOT IS_ABSOLUTE "${install_interface_dir}")
    set(install_interface_dir "\${_IMPORT_PREFIX}/${install_interface_dir}")
  endif()

  target_link_libraries(
    ${TARGET} INTERFACE
    "$<INSTALL_INTERFACE:${install_interface_dir}/${staticlib_output_name}>"
  )
  install(
    TARGETS ${TARGET}
    ${install_export_args}
    LIBRARY DESTINATION ${ARG_INSTALL_DIR}
  )
  install(
    FILES ${staticlib_output_path}
    RENAME ${staticlib_output_name}
    DESTINATION ${ARG_INSTALL_DIR}
  )
endfunction()
