#
# Find libgmock
#
#  LIBGMOCK_DEFINES     - List of defines when using libgmock.
#  LIBGMOCK_INCLUDE_DIR - where to find gmock/gmock.h, etc.
#  LIBGMOCK_LIBRARY     - List of libraries when using libgmock.
#  LIBGMOCK_FOUND       - True if libgmock found.


IF (LIBGMOCK_INCLUDE_DIR)
  # Already in cache, be silent
  SET(LIBGMOCK_FIND_QUIETLY TRUE)
ENDIF ()

FIND_PATH(LIBGMOCK_INCLUDE_DIR gmock/gmock.h)

FIND_LIBRARY(LIBGMOCK_LIBRARY gmock_main)

# There isn't currently an easy way to determine if a library was compiled as
# a shared library on Windows, so just assume we've been built against a shared
# build of gmock for now.
SET(LIBGMOCK_DEFINES "GTEST_LINKED_AS_SHARED_LIBRARY=1" CACHE STRING "")

# handle the QUIETLY and REQUIRED arguments and set LIBGMOCK_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBGMOCK DEFAULT_MSG LIBGMOCK_DEFINES LIBGMOCK_LIBRARY LIBGMOCK_INCLUDE_DIR)

MARK_AS_ADVANCED(LIBGMOCK_DEFINES LIBGMOCK_LIBRARY LIBGMOCK_INCLUDE_DIR)
