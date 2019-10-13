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

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

#include <folly/experimental/pushmi/piping.h>
#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/executor/properties.h>
#include <folly/experimental/pushmi/sender/properties.h>

namespace folly {
namespace pushmi {

struct recurse_t {};
constexpr const recurse_t recurse{};

namespace detail {

PUSHMI_INLINE_VAR constexpr struct ownordelegate_t {
} const ownordelegate{};
PUSHMI_INLINE_VAR constexpr struct ownornest_t {
} const ownornest{};

class trampoline_id {
  std::thread::id threadid;
  uintptr_t trampolineid;

 public:
  template <class T>
  explicit trampoline_id(T* trampoline)
      : threadid(std::this_thread::get_id()), trampolineid(trampoline) {}
};

template <class E = std::exception_ptr>
class trampoline;

template<class E, class Tag>
struct trampoline_task
: single_sender_tag::with_values<any_executor_ref<E>>::template with_error<E> {
  using properties = property_set<is_maybe_blocking<>>;

  PUSHMI_TEMPLATE(class SingleReceiver)
  (requires ReceiveValue<
      SingleReceiver,
      any_executor_ref<E>>)
  void submit(SingleReceiver&& what) && {
    trampoline<E>::submit(Tag{}, std::forward<SingleReceiver>(what));
  }
};

template <class E = std::exception_ptr>
class delegator : pipeorigin {
 public:
  using properties = property_set<is_fifo_sequence<>>;

  trampoline_task<E, ownordelegate_t> schedule() {
    return {};
  }
};

template <class E = std::exception_ptr>
class nester : pipeorigin {
 public:
  using properties = property_set<is_fifo_sequence<>>;

  trampoline_task<E, ownornest_t> schedule() {
    return {};
  }
};

template <class E>
class trampoline {
 private:
  using error_type = std::decay_t<E>;
  using work_type = any_receiver<error_type>;
  using queue_type = std::deque<work_type>;
  using pending_type = std::tuple<int, queue_type, bool>;

  inline static pending_type*& owner() {
    static thread_local pending_type* pending = nullptr;
    return pending;
  }

  inline static int& depth(pending_type& p) {
    return std::get<0>(p);
  }

  inline static queue_type& pending(pending_type& p) {
    return std::get<1>(p);
  }

  inline static bool& repeat(pending_type& p) {
    return std::get<2>(p);
  }

 public:
  inline static trampoline_id get_id() {
    return {owner()};
  }

  inline static bool is_owned() {
    return owner() != nullptr;
  }

  template<class SingleReceiver>
  struct delegate_impl {
    using receiver_category = receiver_tag;
    std::decay_t<SingleReceiver> out_;
    void value(){
      delegator<E> that;
      set_value(out_, that);
    }
    void error(E e) noexcept {
      set_error(out_, e);
    }
    void done() {
      set_done(out_);
    }
  };

  template <class Selector, class Derived>
  static void submit(Selector, Derived&, recurse_t) {
    if (!is_owned()) {
      std::terminate();
    }
    repeat(*owner()) = true;
  }

  PUSHMI_TEMPLATE(class SingleReceiver)
  (requires not Same<SingleReceiver, recurse_t>)
  static void submit(
      ownordelegate_t,
      SingleReceiver awhat) {
    delegator<E> that;

    if (is_owned()) {
      // thread already owned

      // poor mans scope guard
      try {
        if (++depth(*owner()) > 100) {
          // defer work to owner
          work_type work(delegate_impl<SingleReceiver>{std::move(awhat)});
          pending(*owner()).push_back(std::move(work));
        } else {
          // dynamic recursion - optimization to balance queueing and
          // stack usage and value interleaving on the same thread.
          set_value(awhat, that);
          set_done(awhat);
        }
      } catch (...) {
        --depth(*owner());
        throw;
      }
      --depth(*owner());
      return;
    }

    // take over the thread

    pending_type pending_store;
    owner() = &pending_store;
    depth(pending_store) = 0;
    repeat(pending_store) = false;
    // poor mans scope guard
    try {
      trampoline<E>::submit(ownornest, std::move(awhat));
    } catch (...) {
      // ignore exceptions while delivering the exception
      try {
        set_error(awhat, std::current_exception());
        for (auto& what : pending(pending_store)) {
          set_error(what, std::current_exception());
        }
      } catch (...) {
      }
      pending(pending_store).clear();

      if (!is_owned()) {
        std::terminate();
      }
      if (!pending(pending_store).empty()) {
        std::terminate();
      }
      owner() = nullptr;
      throw;
    }
    if (!is_owned()) {
      std::terminate();
    }
    if (!pending(pending_store).empty()) {
      std::terminate();
    }
    owner() = nullptr;
  }

  PUSHMI_TEMPLATE(class SingleReceiver)
  (requires not Same<SingleReceiver, recurse_t>)
  static void submit(
      ownornest_t,
      SingleReceiver awhat) {
    delegator<E> that;

    if (!is_owned()) {
      trampoline<E>::submit(ownordelegate, std::move(awhat));
      return;
    }

    auto& pending_store = *owner();

    // static recursion - tail call optimization
    if (pending(pending_store).empty()) {
      bool go = true;
      while (go) {
        repeat(pending_store) = false;
        set_value(awhat, that);
        set_done(awhat);
        go = repeat(pending_store);
      }
    } else {
      pending(pending_store).push_back(work_type{delegate_impl<SingleReceiver>{std::move(awhat)}});
    }

    if (pending(pending_store).empty()) {
      return;
    }

    while (!pending(pending_store).empty()) {
      auto what = std::move(pending(pending_store).front());
      pending(pending_store).pop_front();
      set_value(what);
      set_done(what);
    }
  }
};

} // namespace detail

template <class E = std::exception_ptr>
detail::trampoline_id get_trampoline_id() {
  if (!detail::trampoline<E>::is_owned()) {
    std::terminate();
  }
  return detail::trampoline<E>::get_id();
}

template <class E = std::exception_ptr>
bool owned_by_trampoline() {
  return detail::trampoline<E>::is_owned();
}

template <class E = std::exception_ptr>
inline detail::delegator<E> trampoline() {
  return {};
}
template <class E = std::exception_ptr>
inline detail::nester<E> nested_trampoline() {
  return {};
}

PUSHMI_INLINE_VAR constexpr auto trampolines =
  strandFactory<detail::delegator<std::exception_ptr>>{};

PUSHMI_INLINE_VAR constexpr auto nested_trampolines =
  strandFactory<detail::nester<std::exception_ptr>>{};

namespace detail {

PUSHMI_TEMPLATE(class E)
(requires SenderTo<delegator<E>, recurse_t>)
decltype(auto) repeat(delegator<E>& exec) {
  submit(exec, recurse);
}
template <class AnyExec>
[[noreturn]] void repeat(AnyExec&) {
  std::terminate();
}

} // namespace detail

inline auto repeat() {
  return [](auto& exec) { detail::repeat(exec); };
}

} // namespace pushmi
} // namespace folly
