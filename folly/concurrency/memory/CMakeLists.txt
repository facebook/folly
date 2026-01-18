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
  NAME atomic_read_mostly_main_ptr
  SRCS
    AtomicReadMostlyMainPtr.cpp
  HEADERS
    AtomicReadMostlyMainPtr.h
  DEPS
    folly_executors_inline_executor
  EXPORTED_DEPS
    folly_concurrency_memory_read_mostly_shared_ptr
    folly_indestructible
    folly_synchronization_rcu
)

folly_add_library(
  NAME read_mostly_shared_ptr
  HEADERS
    ReadMostlySharedPtr.h
  EXPORTED_DEPS
    folly_concurrency_memory_tl_ref_count
    folly_function
)

folly_add_library(
  NAME primary_ptr
  HEADERS
    PrimaryPtr.h
  EXPORTED_DEPS
    folly_function
    folly_futures_cleanup
    folly_futures_core
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME tl_ref_count
  HEADERS
    TLRefCount.h
  EXPORTED_DEPS
    folly_synchronization_asymmetric_thread_fence
    folly_synchronization_detail_sleeper
    folly_thread_local
)
