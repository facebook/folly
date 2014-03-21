# - Try to find LIBLZMA
#
# The following variables are optionally searched for defaults
#  LIBLZMA_ROOT_DIR:            Base directory where all LIBLZMA components are found
#
# The following are set after configuration is done: 
#  LIBLZMA_FOUND
#  LIBLZMA_INCLUDE_DIRS
#  LIBLZMA_LIBRARIES
#  LIBLZMA_LIBRARY_DIRS

include(FindPackageHandleStandardArgs)

set(LIBLZMA_ROOT_DIR "" CACHE PATH "Folder contains LIBLZMA") 

find_path(LIBLZMA_INCLUDE_DIR lzma.h
    PATHS ${LIBLZMA_ROOT_DIR})

if(MSVC)
    find_library(LIBLZMA_LIBRARY
        NAMES lzma liblzma
        PATHS ${LIBLZMA_ROOT_DIR})

    set(LIBLZMA_LIBRARY ${LIBLZMA_LIBRARY})
else()
    find_library(LIBLZMA_LIBRARY lzma)
endif()

find_package_handle_standard_args(LIBLZMA DEFAULT_MSG
    LIBLZMA_INCLUDE_DIR LIBLZMA_LIBRARY)


if(LIBLZMA_FOUND)
    set(LIBLZMA_INCLUDE_DIRS ${LIBLZMA_INCLUDE_DIR})
    set(LIBLZMA_LIBRARIES ${LIBLZMA_LIBRARY})
endif()