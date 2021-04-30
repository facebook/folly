/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/detail/StaticSingletonManager.h>

namespace folly {
namespace detail {

class UniqueInstance {
 public:
#if __GNUC__ && __GNUC__ < 7 && !__clang__
  explicit UniqueInstance(...) noexcept {}
#else
  template <template <typename...> class Z, typename... Key, typename... Mapped>
  FOLLY_EXPORT explicit UniqueInstance(
      tag_t<Z<Key..., Mapped...>>, tag_t<Key...>, tag_t<Mapped...>) noexcept {
    Ptr const tmpl = &typeid(key_t<Z>);
    static Ptr const ptrs[] = {&typeid(Key)..., &typeid(Mapped)...};
    auto& global = createGlobal<Value, key_t<Z, Key...>>();
    enforce(tmpl, ptrs, sizeof...(Key), sizeof...(Mapped), global);
  }
#endif

  UniqueInstance(UniqueInstance const&) = delete;
  UniqueInstance(UniqueInstance&&) = delete;
  UniqueInstance& operator=(UniqueInstance const&) = delete;
  UniqueInstance& operator=(UniqueInstance&&) = delete;

 private:
  template <template <typename...> class Z, typename... Key>
  struct key_t {};

  using Ptr = std::type_info const*;
  struct PtrRange {
    Ptr const* b;
    Ptr const* e;
  };
  struct Value {
    Ptr tmpl;
    Ptr const* ptrs;
    std::uint32_t key_size;
    std::uint32_t mapped_size;
  };

  //  Under Clang, this call signature shrinks the aligned and padded size of
  //  call-sites, as compared to a call signature taking Value or Value const&.
  static void enforce(
      Ptr tmpl,
      Ptr const* ptrs,
      std::uint32_t key_size,
      std::uint32_t mapped_size,
      Value& global) noexcept;
};

} // namespace detail
} // namespace folly
