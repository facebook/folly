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

#include <folly/lang/bind/Named.h>
#include <folly/lang/bind/ToStorage.h>

///
/// Adds `Named.h` tags to the `signature_type` from `ToStorage.h`.
///
/// Future: It would be better to refactor the `*_bind_info_t` types NOT to be
/// related by public inheritance -- they don't satisfy Liskov's substitution
/// principle.  Instead, have them quack-a-like while sharing implementation
/// privately (or via composition).  That would mitigate the ever-present
/// slicing risk, and reduce coupling.  If the # of types proliferates besides
/// 2 or 3, this may be relevant:
/// https://gist.github.com/snarkmaster/cfa209a4d05104a78769ca8062f382a0
///

namespace folly::bind::ext { // Details for library authors, not API consumers

struct no_tag_t {};

template <typename>
inline constexpr no_tag_t named_bind_info_tag_v{};

template <auto Tag, typename Base>
inline constexpr auto named_bind_info_tag_v<named_bind_info_t<Tag, Base>> = Tag;

// Named bindings follow the binding policy of the underlying base
template <auto NBI, typename BindingType>
// Use a constraint to avoid object slicing
  requires(!std::is_same_v<
           const no_tag_t,
           decltype(named_bind_info_tag_v<decltype(NBI)>)>)
class bind_to_storage_policy<ext::binding_t<NBI, BindingType>> {
 private:
  using underlying =
      bind_to_storage_policy<ext::binding_t<NBI.as_base(), BindingType>>;

  template <typename Base>
  static constexpr auto signature_type_for(
      const named_bind_info_t<ext::self_id_t{}, Base>&)
      -> self_id_type<typename underlying::signature_type>;

  template <literal_string Str, typename Base>
  static constexpr auto signature_type_for(const named_bind_info_t<Str, Base>&)
      -> id_type<Str, typename underlying::signature_type>;

 public:
  using storage_type = typename underlying::storage_type;
  using signature_type = decltype(signature_type_for(NBI));
};

} // namespace folly::bind::ext
