/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>

namespace folly {

namespace detail {

// Implemented this way because of a bug in Clang for ARMv7, which gives the
// wrong result for `alignof` a `union` with a field of each scalar type.
constexpr size_t max_align_(std::size_t a) {
  return a;
}
template <typename... Es>
constexpr std::size_t max_align_(std::size_t a, std::size_t e, Es... es) {
  return !(a < e) ? a : max_align_(e, es...);
}
template <typename... Ts>
struct max_align_t_ {
  static constexpr std::size_t value = max_align_(0u, alignof(Ts)...);
};
using max_align_v_ = max_align_t_<
    long double,
    double,
    float,
    long long int,
    long int,
    int,
    short int,
    bool,
    char,
    char16_t,
    char32_t,
    wchar_t,
    void*,
    std::max_align_t>;

} // namespace detail

// max_align_v is the alignment of max_align_t.
//
// max_align_t is a type which is aligned at least as strictly as the
// most-aligned basic type (see the specification of std::max_align_t). This
// implementation exists because 32-bit iOS platforms have a broken
// std::max_align_t (see below).
//
// You should refer to this as `::folly::max_align_t` in portable code, even if
// you have `using namespace folly;` because C11 defines a global namespace
// `max_align_t` type.
//
// To be certain, we consider every non-void fundamental type specified by the
// standard. On most platforms `long double` would be enough, but iOS 32-bit
// has an 8-byte aligned `double` and `long long int` and a 4-byte aligned
// `long double`.
//
// So far we've covered locals and other non-allocated storage, but we also need
// confidence that allocated storage from `malloc`, `new`, etc will also be
// suitable for objects with this alignment requirement.
//
// Apple document that their implementation of malloc will issue 16-byte
// granularity chunks for small allocations (large allocations are page-size
// granularity and page-aligned). We think that allocated storage will be
// suitable for these objects based on the following assumptions:
//
// 1. 16-byte granularity also means 16-byte aligned.
// 2. `new` and other allocators follow the `malloc` rules.
//
// We also have some anecdotal evidence: we don't see lots of misaligned-storage
// crashes on 32-bit iOS apps that use `double`.
//
// Apple's allocation reference: http://bit.ly/malloc-small
constexpr std::size_t max_align_v = detail::max_align_v_::value;
struct alignas(max_align_v) max_align_t {};

} // namespace folly
