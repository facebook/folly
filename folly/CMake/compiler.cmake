# C++ 11 feature checks
# bundled helper for c++11 support is in cmake next
include(FindCXXFeatures)
find_package(CXXFeatures)
# TODO: add additional not worked around features
# Also note that noexcept is not checked for!
set(needed_features
    CXXFeatures_auto
    CXXFeatures_initializer_list
)

foreach(i ${needed_features})
    if(NOT ${i}_FOUND)
        message(FATAL_ERROR "CXX feature \"${i}\" is not supported by the compiler")
endif()
endforeach()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX11_COMPILER_FLAGS}")
message(STATUS "C++ Flags: ${CMAKE_CXX_FLAGS}")