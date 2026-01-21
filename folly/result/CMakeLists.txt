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
  NAME rich_error_base
  SRCS
    rich_error_base.cpp
    rich_exception_ptr.cpp
  HEADERS
    rich_error_base.h
    rich_exception_ptr.h
  DEPS
    folly_demangle
  EXPORTED_DEPS
    folly_cpp_attributes
    folly_lang_exception
    folly_lang_pretty
    folly_portability
    folly_portability_source_location
    folly_result_detail_rich_exception_ptr_storage
    folly_result_rich_error_fwd
    folly_unit
    folly_utility
)

folly_add_library(
  NAME coded_rich_error
  HEADERS
    coded_rich_error.h
  EXPORTED_DEPS
    folly_result_rich_error
    folly_result_rich_error_base
    folly_result_rich_error_code
    folly_result_rich_msg
)

folly_add_library(
  NAME enrich_non_value
  SRCS
    enrich_non_value.cpp
  HEADERS
    enrich_non_value.h
  EXPORTED_DEPS
    folly_lang_safe_assert
    folly_portability
    folly_result_result
    folly_result_rich_error
    folly_result_rich_msg
)

folly_add_library(
  NAME errc_rich_error
  SRCS
    errc_rich_error.cpp
  HEADERS
    errc_rich_error.h
  EXPORTED_DEPS
    folly_result_coded_rich_error
)

folly_add_library(
  NAME nestable_coded_rich_error
  HEADERS
    nestable_coded_rich_error.h
  EXPORTED_DEPS
    folly_result_coded_rich_error
    folly_result_rich_error
    folly_result_rich_error_base
    folly_result_rich_msg
)

folly_add_library(
  NAME gtest_helpers
  HEADERS
    gtest_helpers.h
  EXPORTED_DEPS
    folly_coro_gtest_helpers
    folly_result_coro
)

folly_add_library(
  NAME coro
  HEADERS
    coro.h
  EXPORTED_DEPS
    folly_coro_coroutine
    folly_coro_error
    folly_lang_must_use_immediately
    folly_result_detail_result_or_unwind
    folly_result_detail_result_promise
    folly_result_result
)

folly_add_library(
  NAME immortal_rich_error
  HEADERS
    immortal_rich_error.h
  EXPORTED_DEPS
    folly_result_detail_ptr_immortal_exception_storage
    folly_result_detail_rich_error_common
    folly_result_rich_error_base
)

folly_add_library(
  NAME or_unwind_rich
  HEADERS
    or_unwind_rich.h
  EXPORTED_DEPS
    folly_result_coro
    folly_result_enrich_non_value
)

folly_add_library(
  NAME result
  SRCS
    result.cpp
  HEADERS
    result.h
  DEPS
    folly_indestructible
  EXPORTED_DEPS
    folly_coro_coroutine
    folly_exception_wrapper
    folly_expected
    folly_lang_align
    folly_lang_rvalue_reference_wrapper
    folly_operation_cancelled
    folly_portability_gtest_prod
    folly_result_rich_error_base
)

folly_add_library(
  NAME rich_error
  HEADERS
    rich_error.h
  EXPORTED_DEPS
    folly_portability
    folly_result_detail_rich_error_common
    folly_result_rich_error_base
)

folly_add_library(
  NAME rich_error_code
  SRCS
    rich_error_code.cpp
  HEADERS
    rich_error_code.h
  DEPS
    folly_lang_exception
  EXPORTED_DEPS
    folly_portability
    folly_result_rich_error_base
)

folly_add_library(
  NAME rich_error_fwd
  HEADERS
    rich_error_fwd.h
  EXPORTED_DEPS
    folly_portability
)

folly_add_library(
  NAME rich_msg
  HEADERS
    rich_msg.h
  EXPORTED_DEPS
    folly_lang_exception
    folly_lang_safe_assert
    folly_portability_source_location
    folly_utility
)

folly_add_library(
  NAME try
  HEADERS
    try.h
  EXPORTED_DEPS
    folly_result_result
    folly_try
)

folly_add_library(
  NAME value_only_result
  HEADERS
    value_only_result.h
    value_only_result_coro.h
  EXPORTED_DEPS
    folly_result_coro
)

add_subdirectory(detail)
