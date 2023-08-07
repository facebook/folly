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

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <folly/Traits.h>
#include <folly/container/Iterator.h>
#include <folly/functional/ApplyTuple.h>

// Utility functions for container implementors

namespace folly {
namespace detail {

template <typename KeyType, typename Alloc>
struct TemporaryEmplaceKey {
  TemporaryEmplaceKey(TemporaryEmplaceKey const&) = delete;
  TemporaryEmplaceKey(TemporaryEmplaceKey&&) = delete;

  template <typename... Args>
  TemporaryEmplaceKey(Alloc& a, std::tuple<Args...>&& args) : alloc_(a) {
    auto p = &value();
    apply(
        [&, p](auto&&... inner) {
          std::allocator_traits<Alloc>::construct(
              alloc_, p, std::forward<decltype(inner)>(inner)...);
        },
        std::move(args));
  }

  ~TemporaryEmplaceKey() {
    std::allocator_traits<Alloc>::destroy(alloc_, &value());
  }

  KeyType& value() { return *static_cast<KeyType*>(static_cast<void*>(&raw_)); }

  Alloc& alloc_;
  std::aligned_storage_t<sizeof(KeyType), alignof(KeyType)> raw_;
};

// A map's emplace(args...) function takes arguments that can be used to
// construct a pair<key_type const, mapped_type>, but that construction
// only needs to take place if the key is not present in the container.
// callWithExtractedKey helps to handle this efficiently by looking for a
// reference to the key within the args list.  If the search is successful
// then the search can be performed without constructing any temporaries.
// If the search is not successful then callWithExtractedKey constructs
// a temporary key_type and a new argument list suitable for constructing
// the entire value_type if necessary.
//
// callWithExtractedKey(a, f, args...) will call f(k, args'...), where
// k is the key and args'... is an argument list that can be used to
// construct a pair of key and mapped value.  Note that this means f gets
// the key twice.
//
// In some cases a temporary key must be constructed.  This is accomplished
// with std::allocator_traits<>::construct, and the temporary will be
// destroyed with std::allocator_traits<>::destroy.  Using the allocator's
// construct method reduces unnecessary copies for pmr allocators.
//
// callWithExtractedKey supports heterogeneous lookup with the UsableAsKey
// template parameter.  If a single key argument of type K is found in
// args... then it will be passed directly to f if it is either KeyType or
// if UsableAsKey<remove_cvref_t<K>>::value is true.  If you don't care
// about heterogeneous lookup you can just pass a single-arg template
// that extends std::false_type.

// TODO(T31574848): We can remove the std::enable_if_t once we no longer
// target platforms without N4387 ("perfect initialization" for pairs
// and tuples).  libstdc++ at gcc-6.1.0 is the first release that contains
// the improved set of pair constructors.
template <
    typename KeyType,
    typename MappedType,
    typename Func,
    typename UsableKeyType,
    typename Arg1,
    typename Arg2,
    std::enable_if_t<
        std::is_constructible<
            std::pair<KeyType const, MappedType>,
            Arg1&&,
            Arg2&&>::value,
        int> = 0>
auto callWithKeyAndPairArgs(
    Func&& f,
    UsableKeyType const& key,
    std::tuple<Arg1>&& first_args,
    std::tuple<Arg2>&& second_args) {
  return f(
      key,
      std::forward<Arg1>(std::get<0>(first_args)),
      std::forward<Arg2>(std::get<0>(second_args)));
}

template <
    typename KeyType,
    typename MappedType,
    typename Func,
    typename UsableKeyType,
    typename... Args1,
    typename... Args2>
auto callWithKeyAndPairArgs(
    Func&& f,
    UsableKeyType const& key,
    std::tuple<Args1...>&& first_args,
    std::tuple<Args2...>&& second_args) {
  return f(
      key,
      std::piecewise_construct,
      std::move(first_args),
      std::move(second_args));
}

template <typename>
using ExactKeyMatchOnly = std::false_type;

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename Arg1,
    typename... Args2,
    std::enable_if_t<
        std::is_same<remove_cvref_t<Arg1>, KeyType>::value ||
            UsableAsKey<remove_cvref_t<Arg1>>::value,
        int> = 0>
auto callWithExtractedKey(
    Alloc&,
    Func&& f,
    std::piecewise_construct_t,
    std::tuple<Arg1>&& first_args,
    std::tuple<Args2...>&& second_args) {
  // we found a usable key in the args :)
  auto const& key = std::get<0>(first_args);
  return callWithKeyAndPairArgs<KeyType, MappedType>(
      std::forward<Func>(f),
      key,
      std::tuple<Arg1&&>(std::move(first_args)),
      std::tuple<Args2&&...>(std::move(second_args)));
}

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename... Args1,
    typename... Args2>
auto callWithExtractedKey(
    Alloc& a,
    Func&& f,
    std::piecewise_construct_t,
    std::tuple<Args1...>&& first_args,
    std::tuple<Args2...>&& second_args) {
  // we will need to materialize a temporary key :(
  TemporaryEmplaceKey<KeyType, Alloc> key(
      a, std::tuple<Args1&&...>(std::move(first_args)));
  return callWithKeyAndPairArgs<KeyType, MappedType>(
      std::forward<Func>(f),
      const_cast<KeyType const&>(key.value()),
      std::forward_as_tuple(std::move(key.value())),
      std::tuple<Args2&&...>(std::move(second_args)));
}

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func>
auto callWithExtractedKey(Alloc& a, Func&& f) {
  return callWithExtractedKey<KeyType, MappedType, UsableAsKey>(
      a,
      std::forward<Func>(f),
      std::piecewise_construct,
      std::tuple<>{},
      std::tuple<>{});
}

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename U1,
    typename U2>
auto callWithExtractedKey(Alloc& a, Func&& f, U1&& x, U2&& y) {
  return callWithExtractedKey<KeyType, MappedType, UsableAsKey>(
      a,
      std::forward<Func>(f),
      std::piecewise_construct,
      std::forward_as_tuple(std::forward<U1>(x)),
      std::forward_as_tuple(std::forward<U2>(y)));
}

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename U1,
    typename U2>
auto callWithExtractedKey(Alloc& a, Func&& f, std::pair<U1, U2> const& p) {
  return callWithExtractedKey<KeyType, MappedType, UsableAsKey>(
      a,
      std::forward<Func>(f),
      std::piecewise_construct,
      std::forward_as_tuple(p.first),
      std::forward_as_tuple(p.second));
}

template <
    typename KeyType,
    typename MappedType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename U1,
    typename U2>
auto callWithExtractedKey(Alloc& a, Func&& f, std::pair<U1, U2>&& p) {
  // std::move(p.first) is wrong because if U1 is an lvalue reference the
  // result will incorrectly be an rvalue ref.  static_cast here allows
  // proper ref collapsing
  return callWithExtractedKey<KeyType, MappedType, UsableAsKey>(
      a,
      std::forward<Func>(f),
      std::piecewise_construct,
      std::forward_as_tuple(static_cast<U1&&>(p.first)),
      std::forward_as_tuple(static_cast<U2&&>(p.second)));
}

// callWithConstructedKey is the set container analogue of
// callWithExtractedKey

template <
    typename KeyType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename Arg,
    std::enable_if_t<
        std::is_same<remove_cvref_t<Arg>, KeyType>::value ||
            UsableAsKey<remove_cvref_t<Arg>>::value,
        int> = 0>
auto callWithConstructedKey(Alloc&, Func&& f, Arg&& arg) {
  // we found a usable key in the args :)
  auto const& key = arg;
  return f(key, std::forward<Arg>(arg));
}

template <
    typename KeyType,
    template <typename> class UsableAsKey = ExactKeyMatchOnly,
    typename Alloc,
    typename Func,
    typename... Args>
auto callWithConstructedKey(Alloc& a, Func&& f, Args&&... args) {
  // we will need to materialize a temporary key :(
  TemporaryEmplaceKey<KeyType, Alloc> key(
      a, std::forward_as_tuple(std::forward<Args>(args)...));
  return f(const_cast<KeyType const&>(key.value()), std::move(key.value()));
}

// Traits to simplify deduction guides implementation for containers.

// SFINAE constraint to test whether a type is an allocator according to
// is_allocator trait.
template <typename T>
using RequireAllocator = std::enable_if_t<is_allocator_v<T>, T>;

template <typename T>
using RequireNotAllocator = std::enable_if_t<!is_allocator_v<T>, T>;

template <typename T>
using RequireInputIterator =
    std::enable_if_t<iterator_category_matches_v<T, std::input_iterator_tag>>;

} // namespace detail
} // namespace folly
