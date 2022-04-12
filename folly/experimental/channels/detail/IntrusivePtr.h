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

#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace folly {
namespace channels {
namespace detail {

/**
 * An intrusive_ptr is like an std::shared_ptr. However, unlike a shared_ptr,
 * the reference count for an intrusive_ptr lives on the object itself. This has
 * two advantages:
 *
 * 1. Each intrusive_ptr is 8 bytes instead of 16 bytes.
 *
 * 2. An intrusive_ptr can be created from a raw pointer/reference, unlike a
 *    shared_ptr.
 *
 * To use intrusive_ptr<T>, ensure that T inherits from IntrusivePtrBase<T>.
 */

template <typename T>
using intrusive_ptr = boost::intrusive_ptr<T>;

template <typename T>
using IntrusivePtrBase =
    boost::intrusive_ref_counter<T, boost::thread_safe_counter>;

template <typename T, typename... Args>
intrusive_ptr<T> make_intrusive(Args&&... args) {
  return intrusive_ptr<T>(new T(std::forward<Args>(args)...));
}
} // namespace detail
} // namespace channels
} // namespace folly
