#
# - Try to find Facebook zstd library
# This will define
# ZSTD_FOUND
# ZSTD_INCLUDE_DIR
# ZSTD_LIBRARY
#

find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
find_library(ZSTD_LIBRARY NAMES zstd)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    ZSTD DEFAULT_MSG
    ZSTD_LIBRARY ZSTD_INCLUDE_DIR
)

if (ZSTD_FOUND)
    message(STATUS "Found Zstd: ${ZSTD_LIBRARY}")
endif()

mark_as_advanced(ZSTD_INCLUDE_DIR ZSTD_LIBRARY)
