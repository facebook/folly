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

#include <cstdint>
#include <typeinfo>

#include <folly/CppAttributes.h>
#include <folly/detail/StaticSingletonManager.h>

namespace folly {
namespace detail {

class UniqueInstance {
 public:
  template <template <typename...> class Z, typename... Key, typename... Mapped>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE explicit UniqueInstance(
      tag_t<Z<Key..., Mapped...>>, tag_t<Key...>, tag_t<Mapped...>) noexcept {
    static Ptr const tmpl = FOLLY_TYPE_INFO_OF(key_t<Z>);
    static Ptr const ptrs[] = {
        FOLLY_TYPE_INFO_OF(Key)..., FOLLY_TYPE_INFO_OF(Mapped)...};
    static Arg arg{
        {tmpl, ptrs, sizeof...(Key), sizeof...(Mapped)},
        {tag<Value, key_t<Z, Key...>>}};
    enforce(arg);
  }

  UniqueInstance(UniqueInstance const&) = delete;
  UniqueInstance(UniqueInstance&&) = delete;
  UniqueInstance& operator=(UniqueInstance const&) = delete;
  UniqueInstance& operator=(UniqueInstance&&) = delete;

 private:
  template <template <typename...> class Z, typename... Key>
  struct key_t {};

  using Ptr = std::type_info const*;
  struct Value {
    Ptr tmpl;
    Ptr const* ptrs;
    std::uint32_t key_size;
    std::uint32_t mapped_size;
  };
  struct Arg {
    Value local;
    StaticSingletonManager::ArgCreate<true> global;
  };

  static void enforce(Arg& arg) noexcept;
};

} // namespace detail
} // namespace folly
