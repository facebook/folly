# Find Cython
#
# This module sets the following variables:
# - Cython_FOUND
# - CYTHON_EXE
# - CYTHON_VERSION_STRING
#
find_program(CYTHON_EXE
             NAMES cython cython3)
if (CYTHON_EXE)
  execute_process(COMMAND ${CYTHON_EXE} --version
                  RESULT_VARIABLE _cython_retcode
                  OUTPUT_VARIABLE _cython_output
                  ERROR_VARIABLE _cython_output
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (${_cython_retcode} EQUAL 0)
    separate_arguments(_cython_output)
    list(GET _cython_output -1 CYTHON_VERSION_STRING)
    message(STATUS "Found Cython Version ${CYTHON_VERSION_STRING}")
  else ()
    message(STATUS "Failed to get Cython version")
  endif ()
else ()
  message(STATUS "Cython not found")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Cython
  REQUIRED_VARS CYTHON_EXE CYTHON_VERSION_STRING
  VERSION_VAR CYTHON_VERSION_STRING
)
