#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>
#include "executor.h"
#include "time_single_deferred.h"

namespace pushmi {

struct recurse_t {};
constexpr const recurse_t recurse{};

struct _pipeable_sender_ {};

namespace detail {

PUSHMI_INLINE_VAR constexpr struct ownordelegate_t {} const ownordelegate {};
PUSHMI_INLINE_VAR constexpr struct ownornest_t {} const ownornest {};

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

template <class E = std::exception_ptr>
class delegator : _pipeable_sender_ {
  using time_point = typename trampoline<E>::time_point;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  time_point now() {
    return trampoline<E>::now();
  }

  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires Receiver<remove_cvref_t<SingleReceiver>, is_single<>>)
  void submit(time_point when, SingleReceiver&& what) {
    trampoline<E>::submit(
        ownordelegate, when, std::forward<SingleReceiver>(what));
  }
};

template <class E = std::exception_ptr>
class nester : _pipeable_sender_ {
  using time_point = typename trampoline<E>::time_point;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  time_point now() {
    return trampoline<E>::now();
  }

  template <class SingleReceiver>
  void submit(time_point when, SingleReceiver&& what) {
    trampoline<E>::submit(ownornest, when, std::forward<SingleReceiver>(what));
  }
};

template <class E>
class trampoline {
 public:
  using time_point = std::chrono::system_clock::time_point;

 private:
  using error_type = std::decay_t<E>;
  using work_type =
     any_single<any_time_executor_ref<error_type, time_point>, error_type>;
  using queue_type = std::deque<std::tuple<time_point, work_type>>;
  using pending_type = std::tuple<int, queue_type, time_point>;

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

  inline static time_point& next(pending_type& p) {
    return std::get<2>(p);
  }

 public:
  inline static trampoline_id get_id() {
    return {owner()};
  }

  inline static bool is_owned() {
    return owner() != nullptr;
  }

  inline static time_point now() {
    return std::chrono::system_clock::now();
  }

  template <class Selector, class Derived>
  static void submit(Selector, Derived&, time_point awhen, recurse_t) {
    if (!is_owned()) {
      abort();
    }
    next(*owner()) = awhen;
  }

  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires not Same<SingleReceiver, recurse_t>)
  static void submit(ownordelegate_t, time_point awhen, SingleReceiver awhat) {
    delegator<E> that;

    if (is_owned()) {
      // thread already owned

      // poor mans scope guard
      try {
        if (++depth(*owner()) > 100 || awhen > trampoline<E>::now()) {
          // defer work to owner
          pending(*owner()).push_back(
              std::make_tuple(awhen, work_type{std::move(awhat)}));
        } else {
          // dynamic recursion - optimization to balance queueing and
          // stack usage and value interleaving on the same thread.
          ::pushmi::set_value(awhat, that);
        }
      } catch(...) {
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
    // poor mans scope guard
    try {
      trampoline<E>::submit(ownornest, awhen, std::move(awhat));
    } catch(...) {

      // ignore exceptions while delivering the exception
      try {
        ::pushmi::set_error(awhat, std::current_exception());
        for (auto& item : pending(pending_store)) {
          auto& what = std::get<1>(item);
          ::pushmi::set_error(what, std::current_exception());
        }
      } catch (...) {
      }
      pending(pending_store).clear();

      if(!is_owned()) { std::abort(); }
      if(!pending(pending_store).empty()) { std::abort(); }
      owner() = nullptr;
      throw;
    }
    if(!is_owned()) { std::abort(); }
    if(!pending(pending_store).empty()) { std::abort(); }
    owner() = nullptr;
  }

  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires not Same<SingleReceiver, recurse_t>)
  static void submit(ownornest_t, time_point awhen, SingleReceiver awhat) {
    delegator<E> that;

    if (!is_owned()) {
      trampoline<E>::submit(ownordelegate, awhen, std::move(awhat));
      return;
    }

    auto& pending_store = *owner();

    // static recursion - tail call optimization
    if (pending(pending_store).empty()) {
      auto when = awhen;
      while (when != time_point{}) {
        if (when > trampoline<E>::now()) {
          std::this_thread::sleep_until(when);
        }
        next(pending_store) = time_point{};
        ::pushmi::set_value(awhat, that);
        when = next(pending_store);
      }
    } else {
      // ensure work is sorted by time
      pending(pending_store)
          .push_back(std::make_tuple(awhen, work_type{std::move(awhat)}));
    }

    if (pending(pending_store).empty()) {
      return;
    }

    while (!pending(pending_store).empty()) {
      std::stable_sort(
          pending(pending_store).begin(),
          pending(pending_store).end(),
          [](auto& lhs, auto& rhs) {
            auto& lwhen = std::get<0>(lhs);
            auto& rwhen = std::get<0>(rhs);
            return lwhen < rwhen;
          });
      auto item = std::move(pending(pending_store).front());
      pending(pending_store).pop_front();
      auto& when = std::get<0>(item);
      if (when > trampoline<E>::now()) {
        std::this_thread::sleep_until(when);
      }
      auto& what = std::get<1>(item);
      any_time_executor_ref<error_type, time_point> anythis{that};
      ::pushmi::set_value(what, anythis);
    }
  }
};

} // namespace detail

template <class E = std::exception_ptr>
detail::trampoline_id get_trampoline_id() {
  if(!detail::trampoline<E>::is_owned()) { std::abort(); }
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

namespace detail {

PUSHMI_TEMPLATE (class E)
  (requires TimeSenderTo<delegator<E>, recurse_t>)
decltype(auto) repeat(delegator<E>& exec) {
  ::pushmi::submit(exec, ::pushmi::now(exec), recurse);
}
template <class AnyExec>
void repeat(AnyExec& exec) {
  std::abort();
}

} // namespace detail

inline auto repeat() {
  return [](auto& exec) { detail::repeat(exec); };
}

} // namespace pushmi
