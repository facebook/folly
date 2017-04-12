# Find dep libraries for libfolly
# current list: pthread, glog, gflags, double-conversion, boost (thread, regex, system)
find_package(Boost 1.49.0 COMPONENTS system thread regex REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

find_package(PTHREADS REQUIRED)
if(PTHREADS_FOUND)
    include_directories(${PTHREADS_INCLUDE_DIR})
    target_link_libraries(libfolly ${PTHREADS_LIBRARY})
endif(PTHREADS_FOUND)

find_package(GFLAGS REQUIRED)
if(GFLAGS_FOUND)
    include_directories(${GFLAGS_INCLUDE_DIR})
    target_link_libraries(libfolly ${GFLAGS_LIBRARY})
endif(GFLAGS_FOUND)

find_package(GLOG REQUIRED)
if(GLOG_FOUND)
    include_directories(${GLOG_INCLUDE_DIR})
    target_link_libraries(libfolly ${GLOG_LIBRARY})
endif(GLOG_FOUND)

find_package(DOUBLECONVERSION REQUIRED)
if(DOUBLECONVERSION_FOUND)
    include_directories(${DOUBLECONVERSION_INCLUDE_DIR})
    target_link_libraries(libfolly ${DOUBLECONVERSION_LIBRARY})
endif(DOUBLECONVERSION_FOUND)

# ibery is optional
find_library( IBERTY_LIBRARY iberty )
if(IBERTY_LIBRARY)
    set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${IBERTY_LIBRARY}" )
endif(IBERTY_LIBRARY)

# clock_gettime is a pain
CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
if(NOT HAVE_CLOCK_GETTIME)
    find_library(LIBRT_LIBRARIES rt)
    if(LIBRT_LIBRARIES)
        set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LIBRT_LIBRARIES}" )
    endif(LIBRT_LIBRARIES)
endif(NOT HAVE_CLOCK_GETTIME)

# IO stuff requires zlib, snappy, lz4 and lzma
find_package(SNAPPY REQUIRED)
if(SNAPPY_FOUND)
    include_directories(${SNAPPY_INCLUDE_DIR})
    target_link_libraries(libfolly ${SNAPPY_LIBRARY})
endif(SNAPPY_FOUND)

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIR})
    target_link_libraries(libfolly ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

find_package(LIBLZMA REQUIRED)
if(LIBLZMA_FOUND)
    include_directories(${LIBLZMA_INCLUDE_DIR})
    target_link_libraries(libfolly ${LIBLZMA_LIBRARY})
endif(LIBLZMA_FOUND)

find_package(LIBLZ4 REQUIRED)
if(LIBLZ4_FOUND)
    include_directories(${LIBLZ4_INCLUDE_DIR})
    target_link_libraries(libfolly ${LIBLZ4_LIBRARY})
endif(LIBLZ4_FOUND)

find_package(LIBEVENT REQUIRED)
if(LIBEVENT_FOUND)
    include_directories(${LIBEVENT_INCLUDE_DIR})
    target_link_libraries(libfolly ${LIBEVENT_LIBRARY})
endif(LIBEVENT_FOUND)