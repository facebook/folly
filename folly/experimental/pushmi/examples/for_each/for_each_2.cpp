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

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <folly/experimental/pushmi/examples/for_each.h>
#include <folly/experimental/pushmi/examples/pool.h>

using namespace folly::pushmi::aliases;

template <class Executor, class Allocator = std::allocator<char>>
auto naive_executor_bulk_target(Executor e, Allocator a = Allocator{}) {
  return [e, a](
             auto init,
             auto selector,
             auto input,
             auto&& func,
             auto sb,
             auto se,
             auto out) mutable {
    using RS = decltype(selector);
    using F = std::conditional_t<
        std::is_lvalue_reference<decltype(func)>::value,
        decltype(func),
        typename std::remove_reference<decltype(func)>::type>;
    using Out = decltype(out);
    try {
      typename std::allocator_traits<Allocator>::template rebind_alloc<char>
          allocState(a);
      using Acc = decltype(init(input));
      struct shared_state_type {
        std::exception_ptr first_exception_{};
        Out destination_;
        RS selector_;
        F func_;
        std::atomic<Acc> accumulation_;
        std::atomic<std::size_t> pending_{1};
        std::atomic<std::size_t> exception_count_{0}; // protects assignment to
                                                      // first exception

        shared_state_type(Out&& destination, RS&& selector, F&& func, Acc acc)
        : destination_((Out&&) destination)
        , selector_((RS&&) selector)
        , func_((F&&) func)
        , accumulation_(acc)
        {}
      };
      auto shared_state = std::allocate_shared<shared_state_type>(
          allocState,
          std::move(out),
          std::move(selector),
          (decltype(func)&&)func,
          init(std::move(input)));
      e.schedule() | op::submit([e, sb, se, shared_state](auto) mutable {
        auto stepDone = [](auto shared_state_) {
          // pending
          if (--shared_state_->pending_ == 0) {
            // first exception
            if (shared_state_->first_exception_) {
              mi::set_error(
                  shared_state_->destination_, shared_state_->first_exception_);
              return;
            }
            try {
              // selector(accumulation)
              auto result = shared_state_->selector_(
                  std::move(shared_state_->accumulation_.load()));
              mi::set_value(shared_state_->destination_, std::move(result));
              mi::set_done(shared_state_->destination_);
            } catch (...) {
              mi::set_error(
                  shared_state_->destination_, std::current_exception());
            }
          }
        };
        for (decltype(sb) idx{sb}; idx != se; ++idx) {
          ++shared_state->pending_;
          e.schedule() | op::submit([shared_state, idx, stepDone](auto) {
            try {
              // this indicates to me that bulk is not the right abstraction
              auto old = shared_state->accumulation_.load();
              Acc step;
              do {
                step = old;
                // func(accumulation, idx)
                shared_state->func_(step, idx);
              } while (!shared_state->accumulation_
                            .compare_exchange_strong(old, step));
            } catch (...) {
              // exception count
              if (shared_state->exception_count_++ == 0) {
                // store first exception
                shared_state->first_exception_ = std::current_exception();
              } // else eat the exception
            }
            stepDone(shared_state);
          });
        }
        stepDone(shared_state);
      });
    } catch (...) {
      e.schedule() |
          op::submit([out = std::move(out), ep = std::current_exception()](
                         auto) mutable { mi::set_error(out, ep); });
    }
  };
}

int main() {
  mi::pool p{std::max(1u, std::thread::hardware_concurrency())};

  std::vector<int> vec(10);

  mi::for_each(
      naive_executor_bulk_target(p.executor()),
      vec.begin(),
      vec.end(),
      [](int& x) { x = 42; });

  assert(
      std::count(vec.begin(), vec.end(), 42) == static_cast<int>(vec.size()));

  std::cout << "OK" << std::endl;

  p.stop();
  p.wait();
}
