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
  NAME named_thread_factory
  HEADERS
    NamedThreadFactory.h
  EXPORTED_DEPS
    folly_conv
    folly_executors_thread_factory_thread_factory
    folly_range
    folly_system_thread_name
)

folly_add_library(
  NAME priority_thread_factory
  SRCS
    PriorityThreadFactory.cpp
  DEPS
    folly_portability_sys_resource
    folly_portability_sys_time
    folly_string
    folly_system_thread_name
  EXPORTED_DEPS
    folly_executors_thread_factory_init_thread_factory
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME init_thread_factory
  HEADERS
    InitThreadFactory.h
  EXPORTED_DEPS
    folly_executors_thread_factory_thread_factory
    folly_scope_guard
)

folly_add_library(
  NAME thread_factory
  HEADERS
    ThreadFactory.h
  EXPORTED_DEPS
    folly_executor
)
