/*
 * Copyright 2017 Facebook, Inc.
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

#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <folly/Functional.h>

namespace folly {

/**
 * Argument tuple for variadic emplace/constructor calls. Stores arguments by
 * (decayed) value. Restores original argument types with reference qualifiers
 * and adornments at unpack time to emulate perfect forwarding.
 *
 * Uses inheritance instead of a type alias to std::tuple so that emplace
 * iterators with implicit unpacking disabled can distinguish between
 * emplace_args and std::tuple parameters.
 *
 * @seealso folly::make_emplace_args
 * @seealso folly::get_emplace_arg
 */
template <typename... Args>
struct emplace_args : public std::tuple<std::decay_t<Args>...> {
  using storage_type = std::tuple<std::decay_t<Args>...>;
  using storage_type::storage_type;
};

/**
 * Pack arguments in a tuple for assignment to a folly::emplace_iterator,
 * folly::front_emplace_iterator, or folly::back_emplace_iterator. The
 * iterator's operator= will unpack the tuple and pass the unpacked arguments
 * to the container's emplace function, which in turn forwards the arguments to
 * the (multi-argument) constructor of the target class.
 *
 * Argument tuples generated with folly::make_emplace_args will be unpacked
 * before being passed to the container's emplace function, even for iterators
 * where implicit_unpack is set to false (so they will not implicitly unpack
 * std::pair or std::tuple arguments to operator=).
 *
 * Arguments are copied (lvalues) or moved (rvalues). To avoid copies and moves,
 * wrap references using std::ref(), std::cref(), and folly::rref(). Beware of
 * dangling references, especially references to temporary objects created with
 * folly::rref().
 *
 * Note that an argument pack created with folly::make_emplace_args is different
 * from an argument pack created with std::make_pair or std::make_tuple.
 * Specifically, passing a std::pair&& or std::tuple&& to an emplace iterator's
 * operator= will pass rvalue references to all fields of that tuple to the
 * container's emplace function, while passing an emplace_args&& to operator=
 * will cast those field references to the exact argument types as passed to
 * folly::make_emplace_args previously. If all arguments have been wrapped by
 * std::reference_wrappers or folly::rvalue_reference_wrappers, the result will
 * be the same as if the container's emplace function had been called directly
 * (perfect forwarding), with no temporary copies of the arguments.
 *
 * @seealso folly::rref
 *
 * @example
 *   class Widget { Widget(int, int); };
 *   std::vector<Widget> makeWidgets(const std::vector<int>& in) {
 *     std::vector<Widget> out;
 *     std::transform(
 *         in.begin(),
 *         in.end(),
 *         folly::back_emplacer(out),
 *         [](int i) { return folly::make_emplace_args(i, i); });
 *     return out;
 *   }
 */
template <typename... Args>
emplace_args<Args...> make_emplace_args(Args&&... args) noexcept(
    noexcept(emplace_args<Args...>(std::forward<Args>(args)...))) {
  return emplace_args<Args...>(std::forward<Args>(args)...);
}

namespace detail {
template <typename Arg>
decltype(auto) unwrap_emplace_arg(Arg&& arg) noexcept {
  return std::forward<Arg>(arg);
}
template <typename Arg>
decltype(auto) unwrap_emplace_arg(std::reference_wrapper<Arg> arg) noexcept {
  return arg.get();
}
template <typename Arg>
decltype(auto) unwrap_emplace_arg(
    folly::rvalue_reference_wrapper<Arg> arg) noexcept {
  return std::move(arg).get();
}
}

/**
 * Getter function for unpacking a single emplace argument.
 *
 * Calling get_emplace_arg on an emplace_args rvalue reference results in
 * perfect forwarding of the original input types. A special case are
 * std::reference_wrapper and folly::rvalue_reference_wrapper objects within
 * folly::emplace_args. These are also unwrapped so that the bare reference is
 * returned.
 *
 * std::get is not a customization point in the standard library, so the
 * cleanest solution was to define our own getter function.
 */
template <size_t I, typename... Args>
decltype(auto) get_emplace_arg(emplace_args<Args...>&& args) noexcept {
  using Out = std::tuple<Args...>;
  return detail::unwrap_emplace_arg(
      std::forward<std::tuple_element_t<I, Out>>(std::get<I>(args)));
}
template <size_t I, typename... Args>
decltype(auto) get_emplace_arg(emplace_args<Args...>& args) noexcept {
  return detail::unwrap_emplace_arg(std::get<I>(args));
}
template <size_t I, typename... Args>
decltype(auto) get_emplace_arg(const emplace_args<Args...>& args) noexcept {
  return detail::unwrap_emplace_arg(std::get<I>(args));
}
template <size_t I, typename Args>
decltype(auto) get_emplace_arg(Args&& args) noexcept {
  return std::get<I>(std::move(args));
}
template <size_t I, typename Args>
decltype(auto) get_emplace_arg(Args& args) noexcept {
  return std::get<I>(args);
}
template <size_t I, typename Args>
decltype(auto) get_emplace_arg(const Args& args) noexcept {
  return std::get<I>(args);
}

namespace detail {
/**
 * Common typedefs and methods for folly::emplace_iterator,
 * folly::front_emplace_iterator, and folly::back_emplace_iterator. Implements
 * everything except the actual emplace function call.
 */
template <typename Derived, typename Container, bool implicit_unpack>
class emplace_iterator_base;

/**
 * Partial specialization of emplace_iterator_base with implicit unpacking
 * disabled.
 */
template <typename Derived, typename Container>
class emplace_iterator_base<Derived, Container, false> {
 public:
  // Iterator traits.
  using iterator_category = std::output_iterator_tag;
  using value_type = void;
  using difference_type = void;
  using pointer = void;
  using reference = void;
  using container_type = Container;

  explicit emplace_iterator_base(Container& container)
      : container(std::addressof(container)) {}

  /**
   * Canonical output operator. Forwards single argument straight to container's
   * emplace function.
   */
  template <typename T>
  Derived& operator=(T&& arg) {
    return static_cast<Derived*>(this)->emplace(std::forward<T>(arg));
  }

  /**
   * Special output operator for packed arguments. Unpacks args and performs
   * variadic call to container's emplace function.
   */
  template <typename... Args>
  Derived& operator=(emplace_args<Args...>& args) {
    return unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(const emplace_args<Args...>& args) {
    return unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(emplace_args<Args...>&& args) {
    return unpackAndEmplace(
        std::move(args), std::index_sequence_for<Args...>{});
  }

  // No-ops.
  Derived& operator*() {
    return static_cast<Derived&>(*this);
  }
  Derived& operator++() {
    return static_cast<Derived&>(*this);
  }
  Derived& operator++(int) {
    return static_cast<Derived&>(*this);
  }

  // We need all of these explicit defaults because the custom operator=
  // overloads disable implicit generation of these functions.
  emplace_iterator_base(const emplace_iterator_base&) = default;
  emplace_iterator_base(emplace_iterator_base&&) noexcept = default;
  emplace_iterator_base& operator=(emplace_iterator_base&) = default;
  emplace_iterator_base& operator=(const emplace_iterator_base&) = default;
  emplace_iterator_base& operator=(emplace_iterator_base&&) noexcept = default;

 protected:
  using Class = emplace_iterator_base;

  template <typename Args, std::size_t... I>
  Derived& unpackAndEmplace(Args& args, std::index_sequence<I...>) {
    return static_cast<Derived*>(this)->emplace(get_emplace_arg<I>(args)...);
  }
  template <typename Args, std::size_t... I>
  Derived& unpackAndEmplace(const Args& args, std::index_sequence<I...>) {
    return static_cast<Derived*>(this)->emplace(get_emplace_arg<I>(args)...);
  }
  template <typename Args, std::size_t... I>
  Derived& unpackAndEmplace(Args&& args, std::index_sequence<I...>) {
    return static_cast<Derived*>(this)->emplace(
        get_emplace_arg<I>(std::move(args))...);
  }

  Container* container;
};

/**
 * Partial specialization of emplace_iterator_base with implicit unpacking
 * enabled.
 *
 * Uses inheritance rather than SFINAE. operator= requires a single argument,
 * which makes it impossible to use std::enable_if or similar.
 */
template <typename Derived, typename Container>
class emplace_iterator_base<Derived, Container, true>
    : public emplace_iterator_base<Derived, Container, false> {
 public:
  using emplace_iterator_base<Derived, Container, false>::emplace_iterator_base;
  using emplace_iterator_base<Derived, Container, false>::operator=;

  /**
   * Special output operator for arguments packed into a std::pair. Unpacks
   * the pair and performs variadic call to container's emplace function.
   */
  template <typename... Args>
  Derived& operator=(std::pair<Args...>& args) {
    return this->unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(const std::pair<Args...>& args) {
    return this->unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(std::pair<Args...>&& args) {
    return this->unpackAndEmplace(
        std::move(args), std::index_sequence_for<Args...>{});
  }

  /**
   * Special output operator for arguments packed into a std::tuple. Unpacks
   * the tuple and performs variadic call to container's emplace function.
   */
  template <typename... Args>
  Derived& operator=(std::tuple<Args...>& args) {
    return this->unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(const std::tuple<Args...>& args) {
    return this->unpackAndEmplace(args, std::index_sequence_for<Args...>{});
  }
  template <typename... Args>
  Derived& operator=(std::tuple<Args...>&& args) {
    return this->unpackAndEmplace(
        std::move(args), std::index_sequence_for<Args...>{});
  }

  // We need all of these explicit defaults because the custom operator=
  // overloads disable implicit generation of these functions.
  emplace_iterator_base(const emplace_iterator_base&) = default;
  emplace_iterator_base(emplace_iterator_base&&) noexcept = default;
  emplace_iterator_base& operator=(emplace_iterator_base&) = default;
  emplace_iterator_base& operator=(const emplace_iterator_base&) = default;
  emplace_iterator_base& operator=(emplace_iterator_base&&) noexcept = default;
};
} // folly::detail

/**
 * Behaves just like std::insert_iterator except that it calls emplace()
 * instead of insert(). Uses perfect forwarding.
 */
template <typename Container, bool implicit_unpack = true>
class emplace_iterator : public detail::emplace_iterator_base<
                             emplace_iterator<Container>,
                             Container,
                             implicit_unpack> {
 private:
  using Base = detail::emplace_iterator_base<
      emplace_iterator<Container>,
      Container,
      implicit_unpack>;

 public:
  emplace_iterator(Container& container, typename Container::iterator i)
      : Base(container), iter(std::move(i)) {}

  using Base::operator=;

  // We need all of these explicit defaults because the custom operator=
  // overloads disable implicit generation of these functions.
  emplace_iterator(const emplace_iterator&) = default;
  emplace_iterator(emplace_iterator&&) noexcept = default;
  emplace_iterator& operator=(emplace_iterator&) = default;
  emplace_iterator& operator=(const emplace_iterator&) = default;
  emplace_iterator& operator=(emplace_iterator&&) noexcept = default;

 protected:
  typename Container::iterator iter;

 private:
  friend typename Base::Class;
  template <typename... Args>
  emplace_iterator& emplace(Args&&... args) {
    iter = this->container->emplace(iter, std::forward<Args>(args)...);
    ++iter;
    return *this;
  }
};

/**
 * Behaves just like std::front_insert_iterator except that it calls
 * emplace_front() instead of insert_front(). Uses perfect forwarding.
 */
template <typename Container, bool implicit_unpack = true>
class front_emplace_iterator : public detail::emplace_iterator_base<
                                   front_emplace_iterator<Container>,
                                   Container,
                                   implicit_unpack> {
 private:
  using Base = detail::emplace_iterator_base<
      front_emplace_iterator<Container>,
      Container,
      implicit_unpack>;

 public:
  using Base::Base;
  using Base::operator=;

  // We need all of these explicit defaults because the custom operator=
  // overloads disable implicit generation of these functions.
  front_emplace_iterator(const front_emplace_iterator&) = default;
  front_emplace_iterator(front_emplace_iterator&&) noexcept = default;
  front_emplace_iterator& operator=(front_emplace_iterator&) = default;
  front_emplace_iterator& operator=(const front_emplace_iterator&) = default;
  front_emplace_iterator& operator=(front_emplace_iterator&&) noexcept =
      default;

 private:
  friend typename Base::Class;
  template <typename... Args>
  front_emplace_iterator& emplace(Args&&... args) {
    this->container->emplace_front(std::forward<Args>(args)...);
    return *this;
  }
};

/**
 * Behaves just like std::back_insert_iterator except that it calls
 * emplace_back() instead of insert_back(). Uses perfect forwarding.
 */
template <typename Container, bool implicit_unpack = true>
class back_emplace_iterator : public detail::emplace_iterator_base<
                                  back_emplace_iterator<Container>,
                                  Container,
                                  implicit_unpack> {
 private:
  using Base = detail::emplace_iterator_base<
      back_emplace_iterator<Container>,
      Container,
      implicit_unpack>;

 public:
  using Base::Base;
  using Base::operator=;

  // We need all of these explicit defaults because the custom operator=
  // overloads disable implicit generation of these functions.
  back_emplace_iterator(const back_emplace_iterator&) = default;
  back_emplace_iterator(back_emplace_iterator&&) noexcept = default;
  back_emplace_iterator& operator=(back_emplace_iterator&) = default;
  back_emplace_iterator& operator=(const back_emplace_iterator&) = default;
  back_emplace_iterator& operator=(back_emplace_iterator&&) noexcept = default;

 private:
  friend typename Base::Class;
  template <typename... Args>
  back_emplace_iterator& emplace(Args&&... args) {
    this->container->emplace_back(std::forward<Args>(args)...);
    return *this;
  }
};

/**
 * Convenience function to construct a folly::emplace_iterator, analogous to
 * std::inserter().
 *
 * Setting implicit_unpack to false will disable implicit unpacking of
 * single std::pair and std::tuple arguments to the iterator's operator=. That
 * may be desirable in case of constructors that expect a std::pair or
 * std::tuple argument.
 */
template <bool implicit_unpack = true, typename Container>
emplace_iterator<Container, implicit_unpack> emplacer(
    Container& c,
    typename Container::iterator i) {
  return emplace_iterator<Container, implicit_unpack>(c, std::move(i));
}

/**
 * Convenience function to construct a folly::front_emplace_iterator, analogous
 * to std::front_inserter().
 *
 * Setting implicit_unpack to false will disable implicit unpacking of
 * single std::pair and std::tuple arguments to the iterator's operator=. That
 * may be desirable in case of constructors that expect a std::pair or
 * std::tuple argument.
 */
template <bool implicit_unpack = true, typename Container>
front_emplace_iterator<Container, implicit_unpack> front_emplacer(
    Container& c) {
  return front_emplace_iterator<Container, implicit_unpack>(c);
}

/**
 * Convenience function to construct a folly::back_emplace_iterator, analogous
 * to std::back_inserter().
 *
 * Setting implicit_unpack to false will disable implicit unpacking of
 * single std::pair and std::tuple arguments to the iterator's operator=. That
 * may be desirable in case of constructors that expect a std::pair or
 * std::tuple argument.
 */
template <bool implicit_unpack = true, typename Container>
back_emplace_iterator<Container, implicit_unpack> back_emplacer(Container& c) {
  return back_emplace_iterator<Container, implicit_unpack>(c);
}
}
