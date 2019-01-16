find_program(_cython
             NAMES cython cython3)
unset(CYTHON_FOUND)
if (_cython)
  execute_process(COMMAND ${_cython} --version
                  RESULT_VARIABLE _cython_retcode
                  OUTPUT_VARIABLE _cython_output
                  ERROR_VARIABLE _cython_output
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (${_cython_retcode} EQUAL 0)
    separate_arguments(_cython_output)
    list(GET _cython_output -1 CYTHON_VERSION_STRING)
    message(STATUS "Found Cython Version ${CYTHON_VERSION_STRING}")
    set(CYTHON_FOUND True)
  else ()
    message(STATUS "Failed to get Cython version")
  endif ()
else ()
  message(STATUS "Cython not found")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cython
                                  REQUIRED_VARS CYTHON_FOUND
                                  VERSION_VAR CYTHON_VERSION_STRING)

