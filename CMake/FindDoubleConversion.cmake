#
# - Try to find Double Conversion library
#
# The following variables are optionally searched for defaults
#  DOUBLECONVERSION_ROOT_DIR:            Base directory where all double-conversion components are found
#
# The following are set after configuration is done: 
#  DOUBLECONVERSION_FOUND
#  DOUBLECONVERSION_INCLUDE_DIRS
#  DOUBLECONVERSION_LIBRARIES
#  DOUBLECONVERSION_LIBRARY_DIRS

include(FindPackageHandleStandardArgs)

set(DOUBLECONVERSION_ROOT_DIR "" CACHE PATH "Folder contains double-conversion")

find_path(DOUBLECONVERSION_INCLUDE_DIR double-conversion/double-conversion.h
    PATHS ${DOUBLECONVERSION_ROOT_DIR})

if(MSVC)
    find_library(DOUBLECONVERSION_LIBRARY_RELEASE double-conversion
        PATHS ${DOUBLECONVERSION_ROOT_DIR}
        PATH_SUFFIXES Release)

    find_library(DOUBLECONVERSION_LIBRARY_DEBUG double-conversion-debug
        PATHS ${DOUBLECONVERSION_ROOT_DIR}
        PATH_SUFFIXES Debug)

    set(DOUBLECONVERSION_LIBRARY optimized ${DOUBLECONVERSION_LIBRARY_RELEASE} debug ${DOUBLECONVERSION_LIBRARY_DEBUG})
else()
    find_library(DOUBLECONVERSION_LIBRARY double-conversion
        PATHS ${DOUBLECONVERSION_ROOT_DIR}
        PATH_SUFFIXES
            lib
            lib64)
endif()

find_package_handle_standard_args(DOUBLECONVERSION DEFAULT_MSG
    DOUBLECONVERSION_INCLUDE_DIR DOUBLECONVERSION_LIBRARY)

if(DOUBLECONVERSION_FOUND)
    set(DOUBLECONVERSION_INCLUDE_DIRS ${DOUBLECONVERSION_INCLUDE_DIR})
    set(DOUBLECONVERSION_LIBRARIES ${DOUBLECONVERSION_LIBRARY})
endif()