# Platform checks
INCLUDE(CheckIncludeFileCXX)
CHECK_INCLUDE_FILE_CXX(fcntl.h HAVE_FCNTL)
CHECK_INCLUDE_FILE_CXX(features.h HAVE_FEATURES)
CHECK_INCLUDE_FILE_CXX(inttypes.h HAVE_INTTYPES)
CHECK_INCLUDE_FILE_CXX(limits.h HAVE_LIMITS)
CHECK_INCLUDE_FILE_CXX(stdint.h HAVE_STDINT)
CHECK_INCLUDE_FILE_CXX(stdlib.h HAVE_STDLIB)
CHECK_INCLUDE_FILE_CXX(string.h HAVE_STRING)
CHECK_INCLUDE_FILE_CXX(time.h HAVE_TIME)
CHECK_INCLUDE_FILE_CXX(unistd.h HAVE_UNISTD)
CHECK_INCLUDE_FILE_CXX(mutex.h HAVE_MUTEX)
CHECK_INCLUDE_FILE_CXX(malloc.h HAVE_MALLOC)
CHECK_INCLUDE_FILE_CXX(emmintrin.h HAVE_EMMINTRIN)
CHECK_INCLUDE_FILE_CXX(byteswap.h FOLLY_HAVE_BYTESWAP_H)
CHECK_INCLUDE_FILE_CXX(bits/c++config.h FOLLY_HAVE_CXX_CONFIG)

INCLUDE(CheckTypeSize)
CHECK_TYPE_SIZE(__int128 HAVE_INT128)
CHECK_TYPE_SIZE(ptrdiff_t HAVE_PTRDIFF_T)

# Platform specific hackery
if(MSVC)
    # add a define for NOMINMAX to keep the min and max macros from overwriting the world
    add_definitions(-DNOMINMAX)
endif(MSVC)

INCLUDE(CheckFunctionExists)
# Checks for library functions.
CHECK_FUNCTION_EXISTS (getdelim HAVE_GETDELIM)
CHECK_FUNCTION_EXISTS (gettimeofday HAVE_GETTIMEOFDAY)
CHECK_FUNCTION_EXISTS (memmove HAVE_MEMMOVE)
CHECK_FUNCTION_EXISTS (memset HAVE_MEMSET)
CHECK_FUNCTION_EXISTS (pow HAVE_POW)
CHECK_FUNCTION_EXISTS (strerror HAVE_STRERROR)
CHECK_FUNCTION_EXISTS (rallocm HAVE_RALLOCM)
CHECK_FUNCTION_EXISTS (malloc_size HAVE_MALLOC_SIZE)
CHECK_FUNCTION_EXISTS (malloc_usable_size HAVE_MALLOC_USABLE_SIZE)
CHECK_FUNCTION_EXISTS (memrchr HAVE_MEMRCHR)

CHECK_FUNCTION_EXISTS (pthread_yield HAVE_PTHREAD_YIELD)
if(NOT HAVE_PTHREAD_YIELD)
    CHECK_INCLUDE_FILE_CXX(sched.h HAVE_SCHED)
    CHECK_FUNCTION_EXISTS (sched_yield HAVE_SCHED_YIELD)
    if(NOT HAVE_SCHED)
        CHECK_INCLUDE_FILE_CXX(pthreads/sched.h HAVE_SCHED)
        CHECK_FUNCTION_EXISTS (sched_yield HAVE_SCHED_YIELD)
    endif()
endif()

INCLUDE(CheckCXXSourceCompiles)

CHECK_CXX_SOURCE_COMPILES("
    #include <type_traits>
    const bool val = std::is_trivially_copyable<bool>::value;
    int main() { return 0; }
"
      FOLLY_HAVE_STD__IS_TRIVIALLY_COPYABLE
)

CHECK_CXX_SOURCE_COMPILES("
    #include <thread>
    #include <chrono>
    void func() { std::this_thread::sleep_for(std::chrono::seconds(1)); }
    int main() { return 0; }
"
    HAVE_STD__THIS_THREAD__SLEEP_FOR
)

CHECK_CXX_SOURCE_COMPILES("
    #include <thread>
    #include <chrono>
    void func() { std::this_thread::sleep_for(std::chrono::seconds(1)); }
    int main() { return 0; }
"
    HAVE_STD__THIS_THREAD__SLEEP_FOR
)

CHECK_CXX_SOURCE_COMPILES("
    #include <type_traits>
    #if !_LIBCPP_VERSION
    #error No libc++
    #endif
    void func() {}
    int main() { return 0; }
"
    FOLLY_USE_LIBCPP
)

CHECK_CXX_SOURCE_COMPILES("
    #include <cstring>
    static constexpr int val = strlen(\"foo\");
    int main() { return 0; }
"
    HAVE_CONSTEXPR_STRLEN
)

INCLUDE(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("
    #pragma GCC diagnostic error \"-Wattributes\"
    extern \"C\" void (*test_ifunc(void))() { return 0; }
    void func() __attribute__((ifunc(\"test_ifunc\")));
    int main() { return 0; }
"
    HAVE_IFUNC
)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/folly/cmake/folly-config.h.cmake ${CMAKE_CURRENT_SOURCE_DIR}/folly/folly-config.h)