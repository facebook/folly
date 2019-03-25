# Copyright (c) Facebook, Inc. and its affiliates.
# - Try to find Glog
# Once done, this will define
#
# GLOG_FOUND - system has Glog
# GLOG_INCLUDE_DIRS - the Glog include directories
# GLOG_LIBRARIES - link these to use Glog

include(FindPackageHandleStandardArgs)

find_library(GLOG_LIBRARY glog
  PATHS ${GLOG_LIBRARYDIR})

find_path(GLOG_INCLUDE_DIR glog/logging.h
  PATHS ${GLOG_INCLUDEDIR})

find_package_handle_standard_args(glog DEFAULT_MSG
  GLOG_LIBRARY
  GLOG_INCLUDE_DIR)

mark_as_advanced(
  GLOG_LIBRARY
  GLOG_INCLUDE_DIR)

set(GLOG_LIBRARIES ${GLOG_LIBRARY})
set(GLOG_INCLUDE_DIRS ${GLOG_INCLUDE_DIR})

if (NOT TARGET glog::glog)
  add_library(glog::glog UNKNOWN IMPORTED)
  set_target_properties(glog::glog PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLOG_INCLUDE_DIRS}")
  set_target_properties(glog::glog PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES "C" IMPORTED_LOCATION "${GLOG_LIBRARIES}")
endif()
