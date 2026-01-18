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
  NAME contains
  SRCS
    Contains.cpp
  HEADERS
    Contains.h
  DEPS
    folly_algorithm_simd_detail_simd_contains_impl
  EXPORTED_DEPS
    folly_algorithm_simd_detail_traits
    folly_c_portability
)

folly_add_library(
  NAME find_first_of
  HEADERS
    find_first_of.h
  EXPORTED_DEPS
    folly_algorithm_simd_movemask
    folly_container_span
    folly_container_sparse_byte_set
    folly_lang_align
    folly_lang_bits
    folly_lang_hint
    folly_portability
    folly_utility
)

folly_add_library(
  NAME find_first_of_extra
  HEADERS
    find_first_of_extra.h
  EXPORTED_DEPS
    folly_container_span
    folly_range
)

folly_add_library(
  NAME find_fixed
  HEADERS
    FindFixed.h
  EXPORTED_DEPS
    folly_algorithm_simd_detail_traits
    folly_algorithm_simd_movemask
    folly_portability
)

folly_add_library(
  NAME ignore
  HEADERS
    Ignore.h
  EXPORTED_DEPS
    folly_lang_bits
)

folly_add_library(
  NAME movemask
  HEADERS
    Movemask.h
  EXPORTED_DEPS
    folly_algorithm_simd_ignore
    folly_lang_bits
    folly_portability
)

add_subdirectory(detail)
