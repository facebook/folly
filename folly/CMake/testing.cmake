# gtest configuration for folly
enable_testing()

# use externalproject for gtest to not muddy up the source dir
include(ExternalProject)
set_directory_properties(PROPERTIES EP_PREFIX ${CMAKE_BINARY_DIR}/third_party)

# Add gtest
ExternalProject_Add(
    googletest
    URL https://googletest.googlecode.com/files/gtest-1.7.0.zip
    TIMEOUT 30
    INSTALL_COMMAND "" # disable install
    CMAKE_ARGS -Dgtest_force_shared_crt=ON
    # make sure download, configure and build is logged
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON)

ExternalProject_Get_Property(googletest source_dir)
include_directories(${source_dir}/include)

if(MSVC)
    set(suffix ".lib")
else()
    set(suffix ".a")
endif()

ExternalProject_Get_Property(googletest binary_dir)
SET(GTEST_LIBRARY ${binary_dir}/Debug/gtest${suffix} ${binary_dir}/Debug/gtest_main${suffix})

# include main tests
include(folly/test/tests.cmake)