/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/lang/ToAscii.h>

namespace folly {

namespace detail {

template to_ascii_array<8, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<8, to_ascii_alphabet_lower>::data;
template to_ascii_array<10, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<10, to_ascii_alphabet_lower>::data;
template to_ascii_array<16, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<16, to_ascii_alphabet_lower>::data;
template to_ascii_array<8, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<8, to_ascii_alphabet_upper>::data;
template to_ascii_array<10, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<10, to_ascii_alphabet_upper>::data;
template to_ascii_array<16, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<16, to_ascii_alphabet_upper>::data;

template to_ascii_table<8, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<8, to_ascii_alphabet_lower>::data;
template to_ascii_table<10, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<10, to_ascii_alphabet_lower>::data;
template to_ascii_table<16, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<16, to_ascii_alphabet_lower>::data;
template to_ascii_table<8, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<8, to_ascii_alphabet_upper>::data;
template to_ascii_table<10, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<10, to_ascii_alphabet_upper>::data;
template to_ascii_table<16, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<16, to_ascii_alphabet_upper>::data;

template to_ascii_powers<8, uint64_t>::data_type_ const
    to_ascii_powers<8, uint64_t>::data;
template to_ascii_powers<10, uint64_t>::data_type_ const
    to_ascii_powers<10, uint64_t>::data;
template to_ascii_powers<16, uint64_t>::data_type_ const
    to_ascii_powers<16, uint64_t>::data;

} // namespace detail

} // namespace folly
