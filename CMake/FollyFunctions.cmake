# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

function(auto_sources RETURN_VALUE PATTERN SOURCE_SUBDIRS)
  if ("${SOURCE_SUBDIRS}" STREQUAL "RECURSE")
    SET(PATH ".")
    if (${ARGC} EQUAL 4)
      list(GET ARGV 3 PATH)
    endif ()
  endif()

  if ("${SOURCE_SUBDIRS}" STREQUAL "RECURSE")
    unset(${RETURN_VALUE})
    file(GLOB SUBDIR_FILES "${PATH}/${PATTERN}")
    list(APPEND ${RETURN_VALUE} ${SUBDIR_FILES})

    file(GLOB subdirs RELATIVE ${PATH} ${PATH}/*)

    foreach(DIR ${subdirs})
      if (IS_DIRECTORY ${PATH}/${DIR})
        if (NOT "${DIR}" STREQUAL "CMakeFiles")
          file(GLOB_RECURSE SUBDIR_FILES "${PATH}/${DIR}/${PATTERN}")
          list(APPEND ${RETURN_VALUE} ${SUBDIR_FILES})
        endif()
      endif()
    endforeach()
  else()
    file(GLOB ${RETURN_VALUE} "${PATTERN}")

    foreach (PATH ${SOURCE_SUBDIRS})
      file(GLOB SUBDIR_FILES "${PATH}/${PATTERN}")
      list(APPEND ${RETURN_VALUE} ${SUBDIR_FILES})
    endforeach()
  endif ()

  set(${RETURN_VALUE} ${${RETURN_VALUE}} PARENT_SCOPE)
endfunction(auto_sources)

# Remove all files matching a set of patterns, and,
# optionally, not matching a second set of patterns,
# from a set of lists.
#
# Example:
# This will remove all files in the CPP_SOURCES list
# matching "/test/" or "Test.cpp$", but not matching
# "BobTest.cpp$".
# REMOVE_MATCHES_FROM_LISTS(CPP_SOURCES MATCHES "/test/" "Test.cpp$" IGNORE_MATCHES "BobTest.cpp$")
#
# Parameters:
#
# [...]:
# The names of the lists to remove matches from.
#
# [MATCHES ...]:
# The matches to remove from the lists.
#
# [IGNORE_MATCHES ...]:
# The matches not to remove, even if they match
# the main set of matches to remove.
function(REMOVE_MATCHES_FROM_LISTS)
  set(LISTS_TO_SEARCH)
  set(MATCHES_TO_REMOVE)
  set(MATCHES_TO_IGNORE)
  set(argumentState 0)
  foreach (arg ${ARGN})
    if ("x${arg}" STREQUAL "xMATCHES")
      set(argumentState 1)
    elseif ("x${arg}" STREQUAL "xIGNORE_MATCHES")
      set(argumentState 2)
    elseif (argumentState EQUAL 0)
      list(APPEND LISTS_TO_SEARCH ${arg})
    elseif (argumentState EQUAL 1)
      list(APPEND MATCHES_TO_REMOVE ${arg})
    elseif (argumentState EQUAL 2)
      list(APPEND MATCHES_TO_IGNORE ${arg})
    else()
      message(FATAL_ERROR "Unknown argument state!")
    endif()
  endforeach()

  foreach (theList ${LISTS_TO_SEARCH})
    foreach (entry ${${theList}})
      foreach (match ${MATCHES_TO_REMOVE})
        if (${entry} MATCHES ${match})
          set(SHOULD_IGNORE OFF)
          foreach (ign ${MATCHES_TO_IGNORE})
            if (${entry} MATCHES ${ign})
              set(SHOULD_IGNORE ON)
              break()
            endif()
          endforeach()

          if (NOT SHOULD_IGNORE)
            list(REMOVE_ITEM ${theList} ${entry})
          endif()
        endif()
      endforeach()
    endforeach()
    set(${theList} ${${theList}} PARENT_SCOPE)
  endforeach()
endfunction()

# Automatically create source_group directives for the sources passed in.
function(auto_source_group rootName rootDir)
  file(TO_CMAKE_PATH "${rootDir}" rootDir)
  string(LENGTH "${rootDir}" rootDirLength)
  set(sourceGroups)
  foreach (fil ${ARGN})
    file(TO_CMAKE_PATH "${fil}" filePath)
    string(FIND "${filePath}" "/" rIdx REVERSE)
    if (rIdx EQUAL -1)
      message(FATAL_ERROR "Unable to locate the final forward slash in '${filePath}'!")
    endif()
    string(SUBSTRING "${filePath}" 0 ${rIdx} filePath)

    string(LENGTH "${filePath}" filePathLength)
    string(FIND "${filePath}" "${rootDir}" rIdx)
    if (rIdx EQUAL 0)
      math(EXPR filePathLength "${filePathLength} - ${rootDirLength}")
      string(SUBSTRING "${filePath}" ${rootDirLength} ${filePathLength} fileGroup)

      string(REPLACE "/" "\\" fileGroup "${fileGroup}")
      set(fileGroup "\\${rootName}${fileGroup}")

      list(FIND sourceGroups "${fileGroup}" rIdx)
      if (rIdx EQUAL -1)
        list(APPEND sourceGroups "${fileGroup}")
        source_group("${fileGroup}" REGULAR_EXPRESSION "${filePath}/[^/.]+.(cpp|h)$")
      endif()
    endif()
  endforeach()
endfunction()

# CMake is a pain and doesn't have an easy way to install only the files
# we actually included in our build :(
function(auto_install_files rootName rootDir)
  file(TO_CMAKE_PATH "${rootDir}" rootDir)
  string(LENGTH "${rootDir}" rootDirLength)
  set(sourceGroups)
  foreach (fil ${ARGN})
    file(TO_CMAKE_PATH "${fil}" filePath)
    string(FIND "${filePath}" "/" rIdx REVERSE)
    if (rIdx EQUAL -1)
      message(FATAL_ERROR "Unable to locate the final forward slash in '${filePath}'!")
    endif()
    string(SUBSTRING "${filePath}" 0 ${rIdx} filePath)

    string(LENGTH "${filePath}" filePathLength)
    string(FIND "${filePath}" "${rootDir}" rIdx)
    if (rIdx EQUAL 0)
      math(EXPR filePathLength "${filePathLength} - ${rootDirLength}")
      string(SUBSTRING "${filePath}" ${rootDirLength} ${filePathLength} fileGroup)
      install(FILES ${fil}
              DESTINATION ${INCLUDE_INSTALL_DIR}/${rootName}${fileGroup})
    endif()
  endforeach()
endfunction()

function(folly_define_tests)
  set(directory_count 0)
  set(test_count 0)
  set(currentArg 0)
  while (currentArg LESS ${ARGC})
    if ("x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
      math(EXPR currentArg "${currentArg} + 1")
      if (NOT currentArg LESS ${ARGC})
        message(FATAL_ERROR "Expected base directory!")
      endif()

      set(cur_dir ${directory_count})
      math(EXPR directory_count "${directory_count} + 1")
      set(directory_${cur_dir}_name "${ARGV${currentArg}}")
      # We need a single list of sources to get source_group to work nicely.
      set(directory_${cur_dir}_source_list)

      math(EXPR currentArg "${currentArg} + 1")
      while (currentArg LESS ${ARGC})
        if ("x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
          break()
        elseif ("x${ARGV${currentArg}}" STREQUAL "xTEST" OR
                "x${ARGV${currentArg}}" STREQUAL "xBENCHMARK")
          set(cur_test ${test_count})
          math(EXPR test_count "${test_count} + 1")

          set(test_${cur_test}_is_benchmark $<STREQUAL:"x${ARGV${currentArg}}","xBENCHMARK">)

          math(EXPR currentArg "${currentArg} + 1")
          if (NOT currentArg LESS ${ARGC})
            message(FATAL_ERROR "Expected test name!")
          endif()

          set(test_${cur_test}_name "${ARGV${currentArg}}")
          math(EXPR currentArg "${currentArg} + 1")
          set(test_${cur_test}_directory ${cur_dir})
          set(test_${cur_test}_content_dir)
          set(test_${cur_test}_headers)
          set(test_${cur_test}_sources)
          set(test_${cur_test}_tag)

          set(argumentState 0)
          while (currentArg LESS ${ARGC})
            if ("x${ARGV${currentArg}}" STREQUAL "xHEADERS")
              set(argumentState 1)
            elseif ("x${ARGV${currentArg}}" STREQUAL "xSOURCES")
              set(argumentState 2)
            elseif ("x${ARGV${currentArg}}" STREQUAL "xCONTENT_DIR")
              math(EXPR currentArg "${currentArg} + 1")
              if (NOT currentArg LESS ${ARGC})
                message(FATAL_ERROR "Expected content directory name!")
              endif()
              set(test_${cur_test}_content_dir "${ARGV${currentArg}}")
            elseif ("x${ARGV${currentArg}}" STREQUAL "xTEST" OR
                    "x${ARGV${currentArg}}" STREQUAL "xBENCHMARK" OR
                    "x${ARGV${currentArg}}" STREQUAL "xDIRECTORY")
              break()
            elseif (argumentState EQUAL 0)
              if ("x${ARGV${currentArg}}" STREQUAL "xBROKEN")
                list(APPEND test_${cur_test}_tag "BROKEN")
              elseif ("x${ARGV${currentArg}}" STREQUAL "xHANGING")
                list(APPEND test_${cur_test}_tag "HANGING")
              elseif ("x${ARGV${currentArg}}" STREQUAL "xSLOW")
                list(APPEND test_${cur_test}_tag "SLOW")
              elseif ("x${ARGV${currentArg}}" STREQUAL "xWINDOWS_DISABLED")
                list(APPEND test_${cur_test}_tag "WINDOWS_DISABLED")
              elseif ("x${ARGV${currentArg}}" STREQUAL "xAPPLE_DISABLED")
                list(APPEND test_${cur_test}_tag "APPLE_DISABLED")
              else()
                message(FATAL_ERROR "Unknown test tag '${ARGV${currentArg}}'!")
              endif()
            elseif (argumentState EQUAL 1)
              list(APPEND test_${cur_test}_headers
                "${FOLLY_DIR}/${directory_${cur_dir}_name}${ARGV${currentArg}}"
              )
            elseif (argumentState EQUAL 2)
              list(APPEND test_${cur_test}_sources
                "${FOLLY_DIR}/${directory_${cur_dir}_name}${ARGV${currentArg}}"
              )
            else()
              message(FATAL_ERROR "Unknown argument state!")
            endif()
            math(EXPR currentArg "${currentArg} + 1")
          endwhile()

          list(APPEND directory_${cur_dir}_source_list
            ${test_${cur_test}_sources} ${test_${cur_test}_headers})
        else()
          message(FATAL_ERROR "Unknown argument inside directory '${ARGV${currentArg}}'!")
        endif()
      endwhile()
    else()
      message(FATAL_ERROR "Unknown argument '${ARGV${currentArg}}'!")
    endif()
  endwhile()

  set(cur_dir 0)
  while (cur_dir LESS directory_count)
    source_group("" FILES ${directory_${cur_dir}_source_list})
    math(EXPR cur_dir "${cur_dir} + 1")
  endwhile()

  set(cur_test 0)
  while (cur_test LESS test_count)
    set(cur_test_name ${test_${cur_test}_name})
    set(cur_dir_name ${directory_${test_${cur_test}_directory}_name})
    if ("BROKEN" IN_LIST test_${cur_test}_tag AND NOT BUILD_BROKEN_TESTS)
      message("Skipping broken test ${cur_dir_name}${cur_test_name}, enable with BUILD_BROKEN_TESTS")
    elseif ("SLOW" IN_LIST test_${cur_test}_tag AND NOT BUILD_SLOW_TESTS)
      message("Skipping slow test ${cur_dir_name}${cur_test_name}, enable with BUILD_SLOW_TESTS")
    elseif ("HANGING" IN_LIST test_${cur_test}_tag AND NOT BUILD_HANGING_TESTS)
      message("Skipping hanging test ${cur_dir_name}${cur_test_name}, enable with BUILD_HANGING_TESTS")
    elseif ("WINDOWS_DISABLED" IN_LIST test_${cur_test}_tag AND WIN32 AND NOT BUILD_WINDOWS_DISABLED)
      message("Skipping windows disabled test ${cur_dir_name}${cur_test_name}, enable with BUILD_WINDOWS_DISABLED")
    elseif ("APPLE_DISABLED" IN_LIST test_${cur_test}_tag AND APPLE AND NOT BUILD_APPLE_DISABLED)
      message("Skipping apple disabled test ${cur_dir_name}${cur_test_name}, enable with BUILD_APPLE_DISABLED")
    elseif (${test_${cur_test}_is_benchmark} AND NOT BUILD_BENCHMARKS)
      message("Skipping benchmark ${cur_dir_name}${cur_test_name}, enable with BUILD_BENCHMARKS")
    else()
      add_executable(${cur_test_name}
        ${test_${cur_test}_headers}
        ${test_${cur_test}_sources}
      )
      if (NOT ${test_${cur_test}_is_benchmark})
        if (HAVE_CMAKE_GTEST)
          # If we have CMake's built-in gtest support use it to add each test
          # function as a separate test.
          gtest_add_tests(TARGET ${cur_test_name}
                          WORKING_DIRECTORY "${TOP_DIR}"
                          TEST_PREFIX "${cur_test_name}."
                          TEST_LIST test_cases)
          set_tests_properties(${test_cases} PROPERTIES TIMEOUT 120)
        else()
          # Otherwise add each test executable as a single test.
          add_test(
            NAME ${cur_test_name}
            COMMAND ${cur_test_name}
            WORKING_DIRECTORY "${TOP_DIR}"
          )
          set_tests_properties(${cur_test_name} PROPERTIES TIMEOUT 120)
        endif()
      endif()
      if (NOT "x${test_${cur_test}_content_dir}" STREQUAL "x")
        # Copy the content directory to the output directory tree so that
        # tests can be run easily from Visual Studio without having to change
        # the working directory for each test individually.
        file(
          COPY "${FOLLY_DIR}/${cur_dir_name}${test_${cur_test}_content_dir}"
          DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/folly/${cur_dir_name}${test_${cur_test}_content_dir}"
        )
        add_custom_command(TARGET ${cur_test_name} POST_BUILD COMMAND
          ${CMAKE_COMMAND} ARGS -E copy_directory
            "${FOLLY_DIR}/${cur_dir_name}${test_${cur_test}_content_dir}"
            "$<TARGET_FILE_DIR:${cur_test_name}>/folly/${cur_dir_name}${test_${cur_test}_content_dir}"
          COMMENT "Copying test content for ${cur_test_name}" VERBATIM
        )
      endif()
      # Strip the tailing test directory name for the folder name.
      string(REPLACE "test/" "" test_dir_name "${cur_dir_name}")
      set_property(TARGET ${cur_test_name} PROPERTY FOLDER "Tests/${test_dir_name}")
      target_link_libraries(${cur_test_name} PRIVATE folly_test_support)
      apply_folly_compile_options_to_target(${cur_test_name})
    endif()
    math(EXPR cur_test "${cur_test} + 1")
  endwhile()
endfunction()
