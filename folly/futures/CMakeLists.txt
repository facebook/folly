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

folly_add_library(
  NAME barrier
  SRCS
    Barrier.cpp
  HEADERS
    Barrier.h
  DEPS
    folly_lang_new
    folly_scope_guard
  EXPORTED_DEPS
    folly_futures_core
)

folly_add_library(
  NAME core
  SRCS
    Future.cpp
    HeapTimekeeper.cpp
    Promise.cpp
    ThreadWheelTimekeeper.cpp
  HEADERS
    Future-inl.h
    Future-pre.h
    Future.h
    HeapTimekeeper.h
    Promise-inl.h
    Promise.h
    Retrying.h
    ThreadWheelTimekeeper.h
    WTCallback.h
  DEPS
    folly_container_intrusive_heap
    folly_lang_safe_assert
    folly_likely
    folly_portability_gflags
    folly_singleton
    folly_synchronization_distributed_mutex
    folly_synchronization_relaxed_atomic
    folly_synchronization_saturating_semaphore
    folly_synchronization_wait_options
    folly_system_thread_name
  EXPORTED_DEPS
    folly_chrono
    folly_container_foreach
    folly_coro_traits
    folly_detail_async_trace
    folly_executors_drivable_executor
    folly_executors_executor_with_priority
    folly_executors_global_executor
    folly_executors_inline_executor
    folly_executors_queued_immediate_executor
    folly_executors_timed_drivable_executor
    folly_fibers_core
    folly_functional_invoke
    folly_futures_detail_core
    folly_futures_detail_types
    folly_futures_portability
    folly_io_async_async_base
    folly_lang_exception
    folly_lang_pretty
    folly_optional
    folly_portability
    folly_random
    folly_scope_guard
    folly_traits
    folly_try
    folly_unit
    folly_utility
)

folly_add_library(
  NAME future_splitter
  HEADERS
    FutureSplitter.h
  EXPORTED_DEPS
    folly_futures_core
    folly_futures_shared_promise
    folly_lang_exception
)

folly_add_library(
  NAME futures
  EXPORTED_DEPS
    folly_executors_inline_executor
    folly_executors_manual_executor
    folly_executors_queued_immediate_executor
    folly_executors_scheduled_executor
    folly_futures_barrier
    folly_futures_core
    folly_futures_future_splitter
    folly_futures_shared_promise
)

folly_add_library(
  NAME manual_timekeeper
  SRCS
    ManualTimekeeper.cpp
  HEADERS
    ManualTimekeeper.h
  DEPS
    folly_synchronization_atomic_util
  EXPORTED_DEPS
    folly_futures_core
    folly_synchronized
)

folly_add_library(
  NAME portability
  HEADERS
    Portability.h
  EXPORTED_DEPS
    folly_portability
)

folly_add_library(
  NAME shared_promise
  SRCS
    SharedPromise.cpp
  HEADERS
    SharedPromise-inl.h
    SharedPromise.h
  EXPORTED_DEPS
    folly_executors_inline_executor
    folly_futures_core
    folly_lang_exception
    folly_portability
)

folly_add_library(
  NAME cleanup
  HEADERS
    Cleanup.h
  EXPORTED_DEPS
    folly_futures_core
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

add_subdirectory(detail)
