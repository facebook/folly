# Copyright (c) Facebook, Inc. and its affiliates.
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2.

find_library(RE2_LIBRARY re2)
mark_as_advanced(RE2_LIBRARY)

find_path(RE2_INCLUDE_DIR NAMES re2/re2.h)
mark_as_advanced(RE2_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
     RE2
     REQUIRED_VARS RE2_LIBRARY RE2_INCLUDE_DIR)

if(RE2_FOUND)
  set(RE2_LIBRARY ${RE2_LIBRARY})
  set(RE2_INCLUDE_DIR, ${RE2_INCLUDE_DIR})
endif()
