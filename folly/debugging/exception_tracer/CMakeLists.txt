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

# Exception tracer is excluded when FOLLY_NO_EXCEPTION_TRACER is set
if (NOT FOLLY_NO_EXCEPTION_TRACER)

folly_add_library(
  NAME compatibility
  HEADERS
    Compatibility.h
)

folly_add_library(
  NAME exception_abi
  HEADERS
    ExceptionAbi.h
  EXPORTED_DEPS
    folly_debugging_exception_tracer_compatibility
)

folly_add_library(
  NAME exception_counter
  SRCS
    ExceptionCounterLib.cpp
  HEADERS
    ExceptionCounterLib.h
  DEPS
    folly_debugging_exception_tracer_exception_tracer_callbacks
    folly_debugging_exception_tracer_stacktrace
    folly_experimental_symbolizer_symbolizer
    folly_hash_spooky_hash_v2
    folly_range
    folly_singleton_thread_local
    folly_synchronization_rw_spin_lock
    folly_synchronized
  EXPORTED_DEPS
    folly_debugging_exception_tracer_compatibility
    folly_debugging_exception_tracer_exception_tracer_base
    folly_debugging_exception_tracer_exception_tracer_callbacks
)

folly_add_library(
  NAME exception_counter_static_registration
  SRCS
    ExceptionCounterLibStaticRegistration.cpp
  DEPS
    folly_debugging_exception_tracer_exception_counter
    folly_debugging_exception_tracer_exception_tracer_callbacks
)

folly_add_library(
  NAME exception_tracer
  SRCS
    ExceptionStackTraceLib.cpp
  DEPS
    folly_debugging_exception_tracer_exception_abi
    folly_debugging_exception_tracer_exception_tracer_base
    folly_debugging_exception_tracer_exception_tracer_callbacks
    folly_debugging_exception_tracer_stacktrace
    folly_utility
  EXPORTED_DEPS
    folly_debugging_exception_tracer_exception_tracer_callbacks
)

folly_add_library(
  NAME exception_tracer_base
  SRCS
    ExceptionTracer.cpp
  HEADERS
    ExceptionTracer.h
  DEPS
    folly_debugging_exception_tracer_exception_abi
    folly_debugging_exception_tracer_stacktrace
    folly_experimental_symbolizer_symbolizer
    folly_portability
    folly_string
  EXPORTED_DEPS
    folly_portability_config
)

folly_add_library(
  NAME exception_tracer_callbacks
  SRCS
    ExceptionTracerLib.cpp
  HEADERS
    ExceptionTracerLib.h
  DEPS
    folly_indestructible
    folly_portability
    folly_shared_mutex
    folly_synchronized
  EXPORTED_DEPS
    folly_debugging_exception_tracer_compatibility
)

folly_add_library(
  NAME stacktrace
  SRCS
    StackTrace.cpp
  HEADERS
    StackTrace.h
  DEPS
    folly_experimental_symbolizer_stack_trace
  EXPORTED_DEPS
    folly_portability
)

folly_add_library(
  NAME smart_exception_tracer_singleton
  SRCS
    SmartExceptionTracerSingleton.cpp
  HEADERS
    SmartExceptionTracerSingleton.h
  EXPORTED_DEPS
    folly_container_f14_hash
    folly_debugging_exception_tracer_stacktrace
    folly_synchronized
)

folly_add_library(
  NAME smart_exception_stack_trace_hooks
  SRCS
    SmartExceptionStackTraceHooks.cpp
  DEPS
    folly_debugging_exception_tracer_exception_tracer_callbacks
    folly_debugging_exception_tracer_smart_exception_tracer_singleton
    folly_experimental_symbolizer_symbolizer
)

folly_add_library(
  NAME smart_exception_tracer
  SRCS
    SmartExceptionTracer.cpp
  HEADERS
    SmartExceptionTracer.h
  DEPS
    folly_container_f14_hash
    folly_debugging_exception_tracer_exception_tracer_callbacks
    folly_debugging_exception_tracer_smart_exception_tracer_singleton
    folly_debugging_exception_tracer_stacktrace
    folly_lang_exception
    folly_map_util
    folly_scope_guard
    folly_synchronized
  EXPORTED_DEPS
    folly_debugging_exception_tracer_compatibility
    folly_debugging_exception_tracer_exception_tracer_base
    folly_debugging_exception_tracer_exception_tracer_callbacks
    folly_exception_wrapper
)

endif() # NOT FOLLY_NO_EXCEPTION_TRACER
