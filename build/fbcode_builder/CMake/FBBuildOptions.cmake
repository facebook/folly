# Copyright (c) Facebook, Inc. and its affiliates.

function (fb_activate_static_library_option)
  option(USE_STATIC_DEPS_ON_UNIX
    "If enabled, use static dependencies on unix systems. This is generally discouraged."
    OFF
  )
  # Mark USE_STATIC_DEPS_ON_UNIX as an "advanced" option, since enabling it
  # is generally discouraged.
  mark_as_advanced(USE_STATIC_DEPS_ON_UNIX)

  if(UNIX AND USE_STATIC_DEPS_ON_UNIX)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a" PARENT_SCOPE)
  endif()
endfunction()
