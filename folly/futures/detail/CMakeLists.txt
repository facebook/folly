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
  NAME core
  SRCS
    Core.cpp
  HEADERS
    Core.h
    Types.h
  EXPORTED_DEPS
    folly_executor
    folly_function
    folly_io_async_request_context
    folly_lang_assume
    folly_lang_exception
    folly_lang_switch
    folly_optional
    folly_scope_guard
    folly_synchronization_atomic_util
    folly_try
    folly_utility
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME types
  HEADERS
    Types.h
)
