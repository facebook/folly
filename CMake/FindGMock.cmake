#
# Find libgmock
#
#  LIBGMOCK_INCLUDE_DIR - where to find gmock/gmock.h, etc.
#  LIBGMOCK_LIBRARY     - List of libraries when using libgmock.
#  LIBGMOCK_FOUND       - True if libgmock found.


IF (LIBGMOCK_INCLUDE_DIR)
  # Already in cache, be silent
  SET(LIBGMOCK_FIND_QUIETLY TRUE)
ENDIF ()

FIND_PATH(LIBGMOCK_INCLUDE_DIR gmock/gmock.h)

FIND_LIBRARY(LIBGMOCK_LIBRARY gmock_main)

# handle the QUIETLY and REQUIRED arguments and set LIBGMOCK_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBGMOCK DEFAULT_MSG LIBGMOCK_LIBRARY LIBGMOCK_INCLUDE_DIR)

MARK_AS_ADVANCED(LIBGMOCK_LIBRARY LIBGMOCK_INCLUDE_DIR)
