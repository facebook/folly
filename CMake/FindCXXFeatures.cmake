# - Check which features of the C++ standard the compiler supports
#
# When found it will set the following variables
#
#  CXX11_COMPILER_FLAGS                   - the compiler flags needed to get C++11 features
#
#  CXXFeatures_alignof_FOUND              - alignof keyword
#  CXXFeatures_auto_FOUND                 - auto keyword
#  CXXFeatures_class_override_final_FOUND - override and final keywords for classes and methods
#  CXXFeatures_constexpr_FOUND            - constexpr keyword
#  CXXFeatures_cstdint_header_FOUND       - cstdint header
#  CXXFeatures_decltype_FOUND             - decltype keyword
#  CXXFeatures_defaulted_functions_FOUND  - default keyword for functions
#  CXXFeatures_deleted_functions_FOUND    - delete keyword for functions
#  CXXFeatures_func_identifier_FOUND      - __func__ preprocessor constant
#  CXXFeatures_initializer_list_FOUND     - initializer list
#  CXXFeatures_lambda_FOUND               - lambdas
#  CXXFeatures_long_long_FOUND            - long long signed & unsigned types
#  CXXFeatures_nullptr_FOUND              - nullptr
#  CXXFeatures_rvalue_references_FOUND    - rvalue references
#  CXXFeatures_sizeof_member_FOUND        - sizeof() non-static members
#  CXXFeatures_static_assert_FOUND        - static_assert()
#  CXXFeatures_variadic_templates_FOUND   - variadic templates

#=============================================================================
# Copyright 2011,2012,2013 Rolf Eike Beer <eike@sf-mail.de>
# Copyright 2012 Andreas Weis
# Copyright 2013 Jan Kundrát
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

if (NOT CMAKE_CXX_COMPILER_LOADED)
    message(FATAL_ERROR "CXXFeatures modules only works if language CXX is enabled")
endif ()

cmake_minimum_required(VERSION 2.8.3)

#
### Check for needed compiler flags
#
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++11" _HAS_CXX11_FLAG)
if (_HAS_CXX11_FLAG)
    set(CXX11_COMPILER_FLAGS "-std=c++11")
else ()
    check_cxx_compiler_flag("-std=c++0x" _HAS_CXX0X_FLAG)
    if (_HAS_CXX0X_FLAG)
        set(CXX11_COMPILER_FLAGS "-std=c++0x")
    endif ()
endif ()

function(cxx_check_feature FEATURE_NAME)
    set(RESULT_VAR "CXXFeatures_${FEATURE_NAME}_FOUND")
    if (DEFINED ${RESULT_VAR})
        return()
    endif()

    set(_bindir "${CMAKE_CURRENT_BINARY_DIR}/cxx_${FEATURE_NAME}")

    set(_SRCFILE_BASE ${CMAKE_CURRENT_LIST_DIR}/FindCXXFeatures/cxx11-test-${FEATURE_NAME})
    set(_LOG_NAME "\"${FEATURE_NAME}\"")
    message(STATUS "Checking C++ support for ${_LOG_NAME}")

    set(_SRCFILE "${_SRCFILE_BASE}.cxx")
    set(_SRCFILE_FAIL_COMPILE "${_SRCFILE_BASE}_fail_compile.cxx")

    try_compile(${RESULT_VAR} "${_bindir}" "${_SRCFILE}"
                COMPILE_DEFINITIONS "${CXX11_COMPILER_FLAGS}")

    if (${RESULT_VAR} AND EXISTS ${_SRCFILE_FAIL_COMPILE})
        try_compile(_TMP_RESULT "${_bindir}_fail_compile" "${_SRCFILE_FAIL_COMPILE}"
                    COMPILE_DEFINITIONS "${CXX11_COMPILER_FLAGS}")
        if (_TMP_RESULT)
            set(${RESULT_VAR} FALSE)
        else ()
            set(${RESULT_VAR} TRUE)
        endif ()
    endif ()

    if (${RESULT_VAR})
        message(STATUS "Checking C++ support for ${_LOG_NAME}: works")
    else ()
        message(STATUS "Checking C++ support for ${_LOG_NAME}: not supported")
    endif ()
    set(${RESULT_VAR} "${${RESULT_VAR}}" CACHE INTERNAL "C++ support for ${_LOG_NAME}")
endfunction(cxx_check_feature)

set(_CXX_ALL_FEATURES
    alignof
    auto
    class_override_final
    constexpr
    cstdint_header
    decltype
    defaulted_functions
    deleted_functions
    func_identifier
    initializer_list
    lambda
    long_long
    nullptr
    rvalue_references
    sizeof_member
    static_assert
    variadic_templates
)

if (CXXFeatures_FIND_COMPONENTS)
    foreach (_cxx_feature IN LISTS CXXFeatures_FIND_COMPONENTS)
        list(FIND _CXX_ALL_FEATURES "${_cxx_feature}" _feature_index)
        if (_feature_index EQUAL -1)
            message(FATAL_ERROR "Unknown component: '${_cxx_feature}'")
        endif ()
    endforeach ()
    unset(_feature_index)
else ()
    set(CXXFEATURES_FIND_COMPONENTS ${_CXX_ALL_FEATURES})
endif ()

foreach (_cxx_feature IN LISTS CXXFEATURES_FIND_COMPONENTS)
    cxx_check_feature(${_cxx_feature} ${FEATURE_NAME})
endforeach (_cxx_feature)

include(FindPackageHandleStandardArgs)
set(DUMMY_VAR TRUE)
find_package_handle_standard_args(CXXFeatures REQUIRED_VARS DUMMY_VAR HANDLE_COMPONENTS)
unset(DUMMY_VAR)
unset(_CXX_ALL_FEATURES)
