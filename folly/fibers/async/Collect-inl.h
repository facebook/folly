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

namespace folly {
namespace fibers {
namespace async {

namespace detail {
/**
 * Wrapper around the input iterators to return the wrapped functors
 */
template <class InnerIterator>
struct await_iterator {
  /**
   * Wrapper around the input functor to apply `init_await` on invocation
   */
  struct AwaitWrapper {
    explicit AwaitWrapper(InnerIterator it) : it_(std::move(it)) {}
    auto operator()() {
      return init_await((*it_)());
    }

   private:
    InnerIterator it_;
  };

  using iterator_category = std::input_iterator_tag;
  using value_type = AwaitWrapper;
  using difference_type = std::size_t;
  using pointer = value_type*;
  using reference = value_type&;

  explicit await_iterator(InnerIterator it) : it_(it) {}

  await_iterator& operator++() {
    ++it_;
    return *this;
  }

  await_iterator operator++(int) {
    await_iterator retval = *this;
    ++(*this);
    return retval;
  }

  bool operator==(await_iterator other) const {
    return it_ == other.it_;
  }

  bool operator!=(await_iterator other) const {
    return !(*this == other);
  }

  value_type operator*() const {
    return AwaitWrapper(it_);
  }

 private:
  InnerIterator it_;
};
} // namespace detail

template <class InputIterator, typename FuncType, typename ResultType>
Async<std::vector<
    typename std::enable_if<
        !std::is_same<ResultType, Async<void>>::value,
        async_inner_type_t<ResultType>>::
        type>> inline collectAll(InputIterator first, InputIterator last) {
  return Async(folly::fibers::collectAll(
      detail::await_iterator<InputIterator>(first),
      detail::await_iterator<InputIterator>(last)));
}

template <class InputIterator, typename FuncType, typename ResultType>
typename std::
    enable_if<std::is_same<ResultType, Async<void>>::value, Async<void>>::
        type inline collectAll(InputIterator first, InputIterator last) {
  folly::fibers::collectAll(
      detail::await_iterator<InputIterator>(first),
      detail::await_iterator<InputIterator>(last));

  return {};
}

} // namespace async
} // namespace fibers
} // namespace folly
