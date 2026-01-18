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
  NAME async_closure_fwd
  HEADERS
    AsyncClosure-fwd.h
)

folly_add_library(
  NAME bind_captures
  HEADERS
    BindCaptures.h
  EXPORTED_DEPS
    folly_lang_bind_bind
)

folly_add_library(
  NAME captures
  HEADERS
    Captures.h
  EXPORTED_DEPS
    folly_coro_safe_async_closure_fwd
    folly_coro_safe_detail_define_movable_deep_const_lref_copyable
    folly_detail_tuple
    folly_lang_safe_alias_fwd
    folly_portability
    folly_traits
    folly_utility
)

folly_add_library(
  NAME async_closure
  HEADERS
    AsyncClosure.h
  EXPORTED_DEPS
    folly_coro_safe_detail_async_closure
)

folly_add_library(
  NAME now_task
  HEADERS
    NowTask.h
  EXPORTED_DEPS
    folly_coro_task_wrapper
    folly_lang_safe_alias_fwd
)

folly_add_library(
  NAME safe_task
  HEADERS
    SafeTask.h
  EXPORTED_DEPS
    folly_coro_safe_now_task
    folly_coro_task_wrapper
    folly_lang_safe_alias
)

add_subdirectory(detail)
