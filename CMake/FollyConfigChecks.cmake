include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckSymbolExists)

CHECK_INCLUDE_FILE_CXX(malloc.h FOLLY_HAVE_MALLOC_H)
CHECK_INCLUDE_FILE_CXX(bits/functexcept.h FOLLY_HAVE_BITS_FUNCTEXCEPT_H)

if (FOLLY_HAVE_PTHREAD)
  set(CMAKE_REQUIRED_LIBRARIES
      "${CMAKE_REQUIRED_LIBRARIES} ${LIBPTHREAD_LIBRARIES}")
  set(CMAKE_REQUIRED_INCLUDES
      "${CMAKE_REQUIRED_INCLUDES} ${LIBPTHREAD_INCLUDE_DIRS}")
endif()
check_symbol_exists(pthread_atfork pthread.h FOLLY_HAVE_PTHREAD_ATFORK)

# Unfortunately check_symbol_exists() does not work for memrchr():
# it fails complaining that there are multiple overloaded versions of memrchr()
check_function_exists(memrchr FOLLY_HAVE_MEMRCHR)
check_symbol_exists(preadv sys/uio.h FOLLY_HAVE_PREADV)
check_symbol_exists(pwritev sys/uio.h FOLLY_HAVE_PWRITEV)
check_symbol_exists(clock_gettime time.h FOLLY_HAVE_CLOCK_GETTIME)

check_cxx_source_compiles("
  #pragma GCC diagnostic error \"-Wattributes\"
  extern \"C\" void (*test_ifunc(void))() { return 0; }
  void func() __attribute__((ifunc(\"test_ifunc\")));
  int main() { return 0; }"
  FOLLY_HAVE_IFUNC
)
check_cxx_source_compiles("
  #include <type_traits>
  const bool val = std::is_trivially_copyable<bool>::value;
  int main() { return 0; }"
  FOLLY_HAVE_STD__IS_TRIVIALLY_COPYABLE
)
check_cxx_source_runs("
  int main(int, char**) {
    char buf[64] = {0};
    unsigned long *ptr = (unsigned long *)(buf + 1);
    *ptr = 0xdeadbeef;
    return (*ptr & 0xff) == 0xef ? 0 : 1;
  }"
  FOLLY_HAVE_UNALIGNED_ACCESS
)
check_cxx_source_compiles("
  int main(int argc, char** argv) {
    unsigned size = argc;
    char data[size];
    return 0;
  }"
  FOLLY_HAVE_VLA
)
check_cxx_source_compiles("
  extern \"C\" void configure_link_extern_weak_test() __attribute__((weak));
  int main(int argc, char** argv) {
    return configure_link_extern_weak_test == nullptr;
  }"
  FOLLY_HAVE_WEAK_SYMBOLS
)
