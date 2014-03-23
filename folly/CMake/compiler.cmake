# C++ 11 feature checks
# bundled helper for c++11 support is in cmake next
include(FindCXXFeatures)
find_package(CXXFeatures)

# TODO: add additional not worked around features
set(needed_features
    CXXFeatures_auto
    CXXFeatures_initializer_list
)

foreach(i ${needed_features})
    if(NOT ${i}_FOUND)
        message(FATAL_ERROR "CXX feature \"${i}\" is not supported by the compiler")
endif()
endforeach()

# c++11 features we workaround
# final, override, constexpr, noexcept
if(CXXFeatures_class_override_final_FOUND)
    set(FOLLY_FINAL 1)
    set(FOLLY_OVERRIDE 1)
endif()

if(CXXFeatures_constexpr_FOUND)
    set(FOLLY_HAVE_CONSTEXPR 1)
endif()

if(CXXFeatures_noexcept_FOUND)
    set(FOLLY_HAVE_NOEXCEPT  1)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX11_COMPILER_FLAGS}")
message(STATUS "C++ Flags: ${CMAKE_CXX_FLAGS}")

# Platform specific hackery
if(MSVC)
    # add a define for NOMINMAX to keep the min and max macros from overwriting the world
    add_definitions(-DNOMINMAX)
endif(MSVC)