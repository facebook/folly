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
  NAME simd_any_of
  HEADERS
    SimdAnyOf.h
  EXPORTED_DEPS
    folly_algorithm_simd_detail_simd_for_each
    folly_algorithm_simd_detail_unroll_utils
    folly_c_portability
)

folly_add_library(
  NAME simd_platform
  HEADERS
    SimdPlatform.h
  EXPORTED_DEPS
    folly_algorithm_simd_ignore
    folly_algorithm_simd_movemask
    folly_lang_safe_assert
    folly_portability
)

folly_add_library(
  NAME simd_contains_impl
  HEADERS
    ContainsImpl.h
  EXPORTED_DEPS
    folly_algorithm_simd_detail_simd_any_of
    folly_algorithm_simd_detail_simd_platform
    folly_c_portability
    folly_container_span
)

folly_add_library(
  NAME simd_for_each
  HEADERS
    SimdForEach.h
  EXPORTED_DEPS
    folly_algorithm_simd_detail_unroll_utils
    folly_algorithm_simd_ignore
    folly_c_portability
    folly_lang_align
    folly_traits
)

folly_add_library(
  NAME traits
  HEADERS
    Traits.h
  EXPORTED_DEPS
    folly_c_portability
    folly_container_span
    folly_memory
    folly_traits
)

folly_add_library(
  NAME unroll_utils
  HEADERS
    UnrollUtils.h
  EXPORTED_DEPS
    folly_portability
    folly_traits
)
