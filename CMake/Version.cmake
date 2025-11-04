# Define variables to use to determine the folly RPM package version
string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+.*" "\\1" VERSION_MAJOR "${PACKAGE_VERSION}")
string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+.*" "\\1" VERSION_MINOR "${PACKAGE_VERSION}")
string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VERSION_PATCH "${PACKAGE_VERSION}")
set(VERSION_COMMIT "2020.06.08-0")

find_program(GIT git)
if(GIT)
  execute_process(
      COMMAND ${GIT} describe
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_DESCRIBE_DIRTY
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE GIT_DESCRIBE_RESULT
  )

  if(GIT_DESCRIBE_RESULT EQUAL 0)
    string(REGEX REPLACE "^v([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\-[0-9]+).*" "\\1" VERSION_COMMIT "${GIT_DESCRIBE_DIRTY}")
  else()
      message(FATAL_ERROR "git describe failed with exit code: ${GIT_DESCRIBE_RESULT}")
  endif()
else()
  message(WARNING "git is not found")
endif()

message("Proceeding with version: ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_COMMIT}")

