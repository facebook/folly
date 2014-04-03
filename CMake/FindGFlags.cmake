# - Try to find GFLAGS
#
# The following variables are optionally searched for defaults
#  GFLAGS_ROOT_DIR:            Base directory where all GFLAGS components are found
#
# The following are set after configuration is done: 
#  GFLAGS_FOUND
#  GFLAGS_INCLUDE_DIRS
#  GFLAGS_LIBRARIES
#  GFLAGS_LIBRARY_DIRS

include(FindPackageHandleStandardArgs)

set(GFLAGS_ROOT_DIR "" CACHE PATH "Folder contains Gflags") 

find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
    PATHS ${GFLAGS_ROOT_DIR})

if(MSVC)
    find_library(GFLAGS_LIBRARY_RELEASE 
        NAMES libgflags gflags
        PATHS ${GFLAGS_ROOT_DIR}
        PATH_SUFFIXES Release)

    find_library(GFLAGS_LIBRARY_DEBUG
        NAMES libgflags-debug gflags
        PATHS ${GFLAGS_ROOT_DIR}
        PATH_SUFFIXES Debug)

    set(GFLAGS_LIBRARY optimized ${GFLAGS_LIBRARY_RELEASE} debug ${GFLAGS_LIBRARY_DEBUG})
else()
    find_library(GFLAGS_LIBRARY gflags)
endif()

find_package_handle_standard_args(GFLAGS DEFAULT_MSG
    GFLAGS_INCLUDE_DIR GFLAGS_LIBRARY)


if(GFLAGS_FOUND)
    set(GFLAGS_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIR})
    set(GFLAGS_LIBRARIES ${GFLAGS_LIBRARY})
endif()