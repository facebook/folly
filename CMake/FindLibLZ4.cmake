# - Try to find LIBLZ4
#
# The following variables are optionally searched for defaults
#  LIBLZ4_ROOT_DIR:            Base directory where all LIBLZ4 components are found
#
# The following are set after configuration is done: 
#  LIBLZ4_FOUND
#  LIBLZ4_INCLUDE_DIRS
#  LIBLZ4_LIBRARIES
#  LIBLZ4_LIBRARY_DIRS

include(FindPackageHandleStandardArgs)

set(LIBLZ4_ROOT_DIR "" CACHE PATH "Folder contains LIBLZ4") 

find_path(LIBLZ4_INCLUDE_DIR lz4.h
    PATHS ${LIBLZ4_ROOT_DIR})

if(MSVC)
    find_library(LIBLZ4_LIBRARY
        NAMES lz4 lz464
        PATHS ${LIBLZ4_ROOT_DIR})

    set(LIBLZ4_LIBRARY ${LIBLZ4_LIBRARY})
else()
    find_library(LIBLZ4_LIBRARY lz4)
endif()

find_package_handle_standard_args(LIBLZ4 DEFAULT_MSG
    LIBLZ4_INCLUDE_DIR LIBLZ4_LIBRARY)


if(LIBLZ4_FOUND)
    set(LIBLZ4_INCLUDE_DIRS ${LIBLZ4_INCLUDE_DIR})
    set(LIBLZ4_LIBRARIES ${LIBLZ4_LIBRARY})
endif()