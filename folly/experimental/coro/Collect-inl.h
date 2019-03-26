/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/detail/InlineTask.h>

namespace folly {
namespace coro {
namespace detail {

template <typename T>
T&& getValueOrUnit(Try<T>&& value) {
  return std::move(value).value();
}

Unit getValueOrUnit(Try<void>&& value) {
  value.throwIfFailed();
  return Unit{};
}

template <typename Awaitable, typename Result>
detail::InlineTaskDetached collectAllStartTask(
    Awaitable awaitable,
    Try<Result>& result,
    folly::coro::Baton& baton,
    std::atomic<size_t>& counter) noexcept {
  try {
    if constexpr (std::is_void_v<Result>) {
      co_await static_cast<Awaitable&&>(awaitable);
      result.emplace();
    } else {
      result.emplace(co_await static_cast<Awaitable&&>(awaitable));
    }
  } catch (const std::exception& ex) {
    result.emplaceException(std::current_exception(), ex);
  } catch (...) {
    result.emplaceException(std::current_exception());
  }
  if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    baton.post();
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllTryImpl(
    std::index_sequence<Indices...>,
    SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_try_component_t<SemiAwaitables>...>> {
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    folly::coro::Baton baton;
    std::atomic<size_t> counter(sizeof...(SemiAwaitables));
    Executor* executor = co_await co_current_executor;

    // Use std::initializer_list to ensure that parameter pack is evaluated
    // in-order.
    (void)std::initializer_list<int>{
        (collectAllStartTask(
             folly::coro::co_viaIfAsync(
                 executor, static_cast<SemiAwaitables&&>(awaitables)),
             std::get<Indices>(results),
             baton,
             counter),
         0)...};

    co_await baton;

    co_return results;
  }
}

template <typename... SemiAwaitables, size_t... Indices>
auto collectAllImpl(
    std::index_sequence<Indices...>,
    SemiAwaitables... awaitables)
    -> folly::coro::Task<
        std::tuple<collect_all_component_t<SemiAwaitables>...>> {
  if constexpr (sizeof...(SemiAwaitables) == 0) {
    co_return std::tuple<>{};
  } else {
    std::tuple<collect_all_try_component_t<SemiAwaitables>...> results;

    folly::coro::Baton baton;
    std::atomic<size_t> counter(sizeof...(SemiAwaitables));
    Executor* executor = co_await co_current_executor;

    // Use std::initializer_list to ensure that parameter pack is evaluated
    // in-order.
    (void)std::initializer_list<int>{
        (collectAllStartTask(
             folly::coro::co_viaIfAsync(
                 executor, static_cast<SemiAwaitables&&>(awaitables)),
             std::get<Indices>(results),
             baton,
             counter),
         0)...};

    co_await baton;

    co_return std::tuple<collect_all_component_t<SemiAwaitables>...>{
        getValueOrUnit(std::get<Indices>(std::move(results)))...};
  }
}

} // namespace detail

template <typename... SemiAwaitables>
auto collectAll(SemiAwaitables&&... awaitables) -> folly::coro::Task<std::tuple<
    detail::collect_all_component_t<remove_cvref_t<SemiAwaitables>>...>> {
  return detail::collectAllImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables)>{},
      static_cast<SemiAwaitables&&>(awaitables)...);
}

template <typename... SemiAwaitables>
auto collectAllTry(SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::tuple<detail::collect_all_try_component_t<
        remove_cvref_t<SemiAwaitables>>...>> {
  return detail::collectAllTryImpl(
      std::make_index_sequence<sizeof...(SemiAwaitables)>{},
      static_cast<SemiAwaitables&&>(awaitables)...);
}

} // namespace coro
} // namespace folly
