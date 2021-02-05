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

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <typename Reference, typename Value>
AsyncGenerator<CallbackRecord<Reference>, CallbackRecord<Value>> materialize(
    AsyncGenerator<Reference, Value> source) {
  using EventType = CallbackRecord<Reference>;

  folly::exception_wrapper ex;
  try {
    while (auto item = co_await source.next()) {
      co_yield EventType{callback_record_value, *std::move(item)};
    }
  } catch (const std::exception& e) {
    ex = folly::exception_wrapper{std::current_exception(), e};
  } catch (...) {
    ex = folly::exception_wrapper{std::current_exception()};
  }

  if (ex) {
    co_yield EventType{callback_record_error, std::move(ex)};
  } else {
    co_yield EventType{callback_record_none};
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
