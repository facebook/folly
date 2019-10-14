# Copyright (c) Facebook, Inc. and its affiliates.
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

if (FOLLY_USE_SYMBOLIZER)
  add_library(
    folly_exception_tracer_base
    ExceptionTracer.cpp
    StackTrace.cpp
  )
  apply_folly_compile_options_to_target(folly_exception_tracer_base)
  target_link_libraries(
    folly_exception_tracer_base
    PUBLIC folly
  )

  add_library(
    folly_exception_tracer
    ExceptionStackTraceLib.cpp
    ExceptionTracerLib.cpp
  )
  apply_folly_compile_options_to_target(folly_exception_tracer)
  target_link_libraries(
    folly_exception_tracer
    PUBLIC folly_exception_tracer_base
  )

  add_library(
    folly_exception_counter
    ExceptionCounterLib.cpp
  )
  apply_folly_compile_options_to_target(folly_exception_counter)
  target_link_libraries(
    folly_exception_counter
    PUBLIC folly_exception_tracer
  )

  install(
    FILES
      ExceptionAbi.h
      ExceptionCounterLib.h
      ExceptionTracer.h
      ExceptionTracerLib.h
      StackTrace.h
    DESTINATION
      ${INCLUDE_INSTALL_DIR}/folly/experimental/exception_tracer
  )
  install(
    TARGETS
      folly_exception_tracer_base
      folly_exception_tracer
      folly_exception_counter
    EXPORT folly
    RUNTIME DESTINATION ${BIN_INSTALL_DIR}
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
  )
endif()
