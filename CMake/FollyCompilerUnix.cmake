# Provide an option to control the -std argument for the C++ compiler.
# We don't use CMAKE_CXX_STANDARD since it requires at least CMake 3.8
# to support C++17.
#
# Most users probably want to stick with the default here.  However, gnu++1z
# does change the linkage of how some symbols are emitted (e.g., constexpr
# variables defined in headers).  In case this causes problems for downstream
# libraries that aren't using gnu++1z yet, provide an option to let them still
# override this with gnu++14 if they need to.
set(
  CXX_STD "gnu++1z"
  CACHE STRING
  "The C++ standard argument to pass to the compiler.  Defaults to gnu++1z"
)
mark_as_advanced(CXX_STD)

set(CMAKE_CXX_FLAGS_COMMON "-g -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_COMMON} -O3")

# Note that CMAKE_REQUIRED_FLAGS must be a string, not a list
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=${CXX_STD}")
list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
function(apply_folly_compile_options_to_target THETARGET)
  target_compile_definitions(${THETARGET}
    PRIVATE
      _REENTRANT
      _GNU_SOURCE
      "FOLLY_XLOG_STRIP_PREFIXES=\"${FOLLY_DIR_PREFIXES}\""
  )
  target_compile_options(${THETARGET}
    PRIVATE
      -g
      -std=${CXX_STD}
      -finput-charset=UTF-8
      -fsigned-char
      -Werror
      -Wall
      -Wno-deprecated
      -Wno-deprecated-declarations
      -Wno-sign-compare
      -Wno-unused
      -Wunused-label
      -Wunused-result
      -Wnon-virtual-dtor
      ${FOLLY_CXX_FLAGS}
  )
endfunction()
