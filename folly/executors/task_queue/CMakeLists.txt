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
  NAME blocking_queue
  HEADERS
    BlockingQueue.h
  EXPORTED_DEPS
    folly_c_portability
    folly_optional
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME lifo_sem_mpmc_queue
  HEADERS
    LifoSemMPMCQueue.h
  EXPORTED_DEPS
    folly_executors_task_queue_blocking_queue
    folly_mpmc_queue
    folly_synchronization_lifo_sem
)

folly_add_library(
  NAME priority_lifo_sem_mpmc_queue
  HEADERS
    PriorityLifoSemMPMCQueue.h
  EXPORTED_DEPS
    folly_executor
    folly_executors_task_queue_blocking_queue
    folly_mpmc_queue
    folly_range
    folly_synchronization_lifo_sem
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME priority_unbounded_blocking_queue
  HEADERS
    PriorityUnboundedBlockingQueue.h
  EXPORTED_DEPS
    folly_concurrency_priority_unbounded_queue_set
    folly_constexpr_math
    folly_executor
    folly_executors_task_queue_blocking_queue
    folly_lang_exception
    folly_synchronization_lifo_sem
)

folly_add_library(
  NAME unbounded_blocking_queue
  HEADERS
    UnboundedBlockingQueue.h
  EXPORTED_DEPS
    folly_concurrency_unbounded_queue
    folly_executors_task_queue_blocking_queue
    folly_synchronization_lifo_sem
)

folly_add_library(
  NAME striped_priority_unbounded_blocking_queue
  HEADERS
    StripedPriorityUnboundedBlockingQueue.h
  EXPORTED_DEPS
    folly_concurrency_cache_locality
    folly_concurrency_priority_unbounded_queue_set
    folly_constexpr_math
    folly_executor
    folly_executors_task_queue_blocking_queue
    folly_executors_task_queue_priority_unbounded_blocking_queue
    folly_glog
    folly_lang_exception
    folly_synchronization_striped_throttled_lifo_sem
)
