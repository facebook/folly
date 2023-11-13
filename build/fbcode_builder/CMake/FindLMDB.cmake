# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This software may be used and distributed according to the terms of the
# GNU General Public License version 2.

find_library(LMDB_LIBRARIES NAMES lmdb liblmdb)
mark_as_advanced(LMDB_LIBRARIES)

find_path(LMDB_INCLUDE_DIR NAMES  lmdb.h)
mark_as_advanced(LMDB_INCLUDE_DIR)

find_package_handle_standard_args(
     LMDB
     REQUIRED_VARS LMDB_LIBRARIES LMDB_INCLUDE_DIR)

if(LMDB_FOUND)
  set(LMDB_LIBRARIES ${LMDB_LIBRARIES})
  set(LMDB_INCLUDE_DIR, ${LMDB_INCLUDE_DIR})
endif()
