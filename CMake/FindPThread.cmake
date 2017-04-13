# - Try to find libpthread
#
# Once done this will define
#
#  LIBPTHREAD_FOUND - system has libpthread
#  LIBPTHREAD_INCLUDE_DIRS - the libpthread include directory
#  LIBPTHREAD_LIBRARIES - Link these to use libpthread
#  LIBPTHREAD_DEFINITIONS - Compiler switches required for using libpthread
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


if (LIBPTHREAD_LIBRARIES AND LIBPTHREAD_INCLUDE_DIRS)
  set (LIBPTHREAD_FIND_QUIETLY TRUE)
endif (LIBPTHREAD_LIBRARIES AND LIBPTHREAD_INCLUDE_DIRS)

find_path (LIBPTHREAD_INCLUDE_DIRS NAMES pthread.h)
find_library (LIBPTHREAD_LIBRARIES NAMES pthread)

include (FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBPTHREAD DEFAULT_MSG
  LIBPTHREAD_LIBRARIES LIBPTHREAD_INCLUDE_DIRS)

mark_as_advanced(LIBPTHREAD_INCLUDE_DIRS LIBPTHREAD_LIBRARIES LIBPTHREAD_FOUND)
