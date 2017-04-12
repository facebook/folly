# - Try to find LIBEVENT
#
# The following variables are optionally searched for defaults
#  LIBEVENT_ROOT_DIR:            Base directory where all LIBEVENT components are found
#
# The following are set after configuration is done: 
#  LIBEVENT_FOUND
#  LIBEVENT_INCLUDE_DIRS
#  LIBEVENT_LIBRARIES
#  LIBEVENT_LIBRARY_DIRS

include(FindPackageHandleStandardArgs)

set(LIBEVENT_ROOT_DIR "" CACHE PATH "Folder contains LIBEVENT") 

find_path(LIBEVENT_INCLUDE_DIR event.h
    PATHS ${LIBEVENT_ROOT_DIR})

if(MSVC)
    find_library(LIBEVENT_LIBRARY
        NAMES libevent event
        PATHS ${LIBEVENT_ROOT_DIR})

    set(LIBEVENT_LIBRARY ${LIBEVENT_LIBRARY})
else()
    find_library(LIBEVENT_LIBRARY libevent)
endif()

find_package_handle_standard_args(LIBEVENT DEFAULT_MSG
    LIBEVENT_INCLUDE_DIR LIBEVENT_LIBRARY)


if(LIBEVENT_FOUND)
    set(LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
    set(LIBEVENT_LIBRARIES ${LIBEVENT_LIBRARY})
endif()