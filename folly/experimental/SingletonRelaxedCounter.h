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

#include <array>
#include <atomic>
#include <type_traits>
#include <unordered_set>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/Synchronized.h>
#include <folly/Utility.h>
#include <folly/detail/StaticSingletonManager.h>
#include <folly/detail/ThreadLocalDetail.h>

namespace folly {

namespace detail {

//  SingletonRelaxedCounterBase
//
//  Extracts tag-independent functionality from SingletonRelaxedCounter below
//  to avoid the monomorphization the compiler would otherwise perform.
//
//  Tricks:
//  * Use constexpr vtables as the polymorphization mechanism.
//  * Use double-noinline outer-only-noexcept definitions to shrink code size in
//    inline or monomorphized slow paths.
template <typename Int>
class SingletonRelaxedCounterBase {
 protected:
  using Signed = std::make_signed_t<Int>;
  using Counter = std::atomic<Signed>;

  struct CounterAndCache {
    Counter counter; // valid during LocalLifetime object lifetime
    Counter* cache; // points to counter when counter is valid
  };

  struct CounterRefAndLocal {
    Counter* counter; // refers either to local counter or to global counter
    bool local; // if true, definitely local; if false, could be global
  };

  struct LocalLifetime;

  //  Global
  //
  //  Tracks all of the per-thread/per-dso counters and lifetimes and maintains
  //  a global fallback counter.
  struct Global {
    struct Tracking {
      using CounterSet = std::unordered_set<Counter*>;
      std::unordered_map<Counter*, size_t> locals; // for summing
      std::unordered_map<LocalLifetime*, CounterSet> lifetimes;
    };

    Counter fallback; // used instead of local during thread destruction
    folly::Synchronized<Tracking> tracking;
  };

  using GetGlobal = Global&();
  using GetLocal = CounterAndCache&();
  using GetLifetime = LocalLifetime&();

  struct Arg {
    GetGlobal& global;
    GetLocal& local;
    GetLifetime& lifetime;
  };

  //  LocalLifetime
  //
  //  Manages local().cache, global().tracking, and moving outstanding counts
  //  from local().counter to global().counter during thread destruction.
  //
  //  The counter-set is within Global to reduce per-thread overhead for threads
  //  which do not participate in counter mutations, rather than being a member
  //  field of LocalLifetime. This comes at the cost of the slow path always
  //  acquiring a unique lock on the global mutex.
  struct LocalLifetime {
    FOLLY_NOINLINE void destroy(GetGlobal& get_global) noexcept {
      destroy_(get_global);
    }
    FOLLY_NOINLINE void destroy_(GetGlobal& get_global) {
      auto& global = get_global();
      auto const tracking = global.tracking.wlock();
      auto& lifetimes = tracking->lifetimes[this];
      for (auto ctr : lifetimes) {
        auto const it = tracking->locals.find(ctr);
        if (!--it->second) {
          tracking->locals.erase(it);
          auto const current = ctr->load(std::memory_order_relaxed);
          global.fallback.fetch_add(current, std::memory_order_relaxed);
        }
      }
      tracking->lifetimes.erase(this);
    }

    FOLLY_NOINLINE void track(Global& global, CounterAndCache& state) noexcept {
      track_(global, state);
    }
    FOLLY_NOINLINE void track_(Global& global, CounterAndCache& state) {
      state.cache = &state.counter;
      auto const tracking = global.tracking.wlock();
      auto const inserted = tracking->lifetimes[this].insert(&state.counter);
      tracking->locals[&state.counter] += inserted.second;
    }
  };

  FOLLY_NOINLINE static Int aggregate(GetGlobal& get_global) noexcept {
    return aggregate_(get_global);
  }
  FOLLY_NOINLINE static Int aggregate_(GetGlobal& get_global) {
    auto& global = get_global();
    auto count = global.fallback.load(std::memory_order_relaxed);
    auto const tracking = global.tracking.rlock();
    for (auto const& kvp : tracking->locals) {
      count += kvp.first->load(std::memory_order_relaxed);
    }
    return std::is_unsigned<Int>::value
        ? to_unsigned(std::max(Signed(0), count))
        : count;
  }

  FOLLY_ERASE static void mutate(Signed v, CounterRefAndLocal cl) {
    auto& c = *cl.counter;
    if (cl.local) {
      //  splitting load/store on the local counter is faster than fetch-and-add
      c.store(c.load(std::memory_order_relaxed) + v, std::memory_order_relaxed);
    } else {
      //  but is not allowed on the global counter because mutations may be lost
      c.fetch_add(v, std::memory_order_relaxed);
    }
  }

  FOLLY_NOINLINE static void mutate_slow(Signed v, Arg const& arg) noexcept {
    mutate(v, counter(arg));
  }

  FOLLY_NOINLINE static Counter& counter_slow(Arg const& arg) noexcept {
    auto& global = arg.global();
    if (threadlocal_detail::StaticMetaBase::dying()) {
      return global.fallback;
    }
    auto& state = arg.local();
    arg.lifetime().track(global, state); // idempotent
    auto const cache = state.cache;
    return FOLLY_LIKELY(!!cache) ? *cache : global.fallback;
  }

  FOLLY_ERASE static CounterRefAndLocal counter(Arg const& arg) {
    auto& state = arg.local();
    auto const cache = state.cache; // a copy! null before/after LocalLifetime
    auto const counter = FOLLY_LIKELY(!!cache) ? cache : &counter_slow(arg);
    //  cache is a stale nullptr after the first call to counter_slow; this is
    //  intentional for the side-effect of shrinking the inline fast path
    return CounterRefAndLocal{counter, !!cache};
  }
};

} // namespace detail

//  SingletonRelaxedCounter
//
//  A singleton-per-tag relaxed counter. Optimized for increment/decrement
//  runtime performance under contention and inlined fast path code size.
//
//  The cost of computing the value of the counter is linear in the number of
//  threads which perform increments/decrements, and computing the value of the
//  counter is exclusive with thread exit and dlclose. The result of this
//  computation is not a point-in-time snapshot of increments and decrements
//  summed, but is an approximation which may exclude any subset of increments
//  and decrements that do not happen before the start of the computation.
//
//  Templated over the integral types. When templated over an unsigned integral
//  type, it is assumed that decrements do not exceed increments, and if within
//  computation of the value of the counter more decrements are observed to
//  exceed increments then the excess decrements are ignored. This avoids the
//  scenario of incrementing and decrementing once each in different threads,
//  and concurrently observing a computed value of the counter of 2^64 - 1.
//
//  Templated over the tag types. Each unique pair of integral type and tag type
//  is a different counter.
//
//  Implementation:
//  Uses a thread-local counter when possible to avoid contention, and a global
//  counter as a fallback. The total count at any given time is computed by
//  summing over the global counter plus all of the thread-local counters; since
//  the total sum is not a snapshot of the value at any given point in time, it
//  is a relaxed sum; when the system quiesces (i.e., when no concurrent
//  increments or decrements are happening and no threads are going through
//  thread exit phase), the sum is exact.
//
//  Most of the implementation is in SingletonRelaxedCounterBase to avoid excess
//  monomorphization.
template <typename Int, typename Tag>
class SingletonRelaxedCounter
    : private detail::SingletonRelaxedCounterBase<Int> {
 public:
  static void add(Int value) { mutate(+to_signed(value)); }
  static void sub(Int value) { mutate(-to_signed(value)); }
  static Int count() { return aggregate(global); }

 private:
  using Base = detail::SingletonRelaxedCounterBase<Int>;
  using Base::aggregate;
  using Base::mutate;
  using Base::mutate_slow;
  using typename Base::Arg;
  using typename Base::CounterAndCache;
  using typename Base::GetGlobal;
  using typename Base::Global;
  using typename Base::LocalLifetime;
  using typename Base::Signed;

  struct MonoLocalLifetime : LocalLifetime {
    ~MonoLocalLifetime() noexcept(false) { LocalLifetime::destroy(global); }
  };

  FOLLY_NOINLINE static void mutate_slow(Signed v) noexcept {
    mutate_slow(v, arg);
  }

  FOLLY_ERASE static void mutate(Signed v, void (&slow)(Signed) = mutate_slow) {
    auto const cache = local().cache; // a copy! null before/after LocalLifetime
    //  fun-ref to trick compiler into emitting a tail call
    FOLLY_LIKELY(!!cache) ? mutate(v, {cache, true}) : slow(v);
  }

  static constexpr GetGlobal& global = folly::detail::createGlobal<Global, Tag>;

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static CounterAndCache& local() {
    //  this is a member function local instead of a class member because of
    //  https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66944
    static thread_local CounterAndCache instance;
    return instance;
  }

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static LocalLifetime& lifetime() {
    static thread_local MonoLocalLifetime lifetime;
    return lifetime;
  }

  static constexpr Arg arg{global, local, lifetime};
};

template <typename Int, typename Tag>
constexpr typename SingletonRelaxedCounter<Int, Tag>::Arg
    SingletonRelaxedCounter<Int, Tag>::arg;

template <typename Counted>
class SingletonRelaxedCountableAccess;

//  SingletonRelaxedCountable
//
//  A CRTP base class for making the instances of a type within a process be
//  globally counted. The running counter is a relaxed counter.
//
//  To avoid adding any new names from the base class to the counted type, the
//  count is exposed via a separate type SingletonRelaxedCountableAccess.
//
//  This type is a convenience interface around SingletonRelaxedCounter.
template <typename Counted>
class SingletonRelaxedCountable {
 public:
  SingletonRelaxedCountable() noexcept {
    static_assert(
        std::is_base_of<SingletonRelaxedCountable, Counted>::value, "non-crtp");
    Counter::add(1);
  }
  ~SingletonRelaxedCountable() noexcept {
    static_assert(
        std::is_base_of<SingletonRelaxedCountable, Counted>::value, "non-crtp");
    Counter::sub(1);
  }

  SingletonRelaxedCountable(const SingletonRelaxedCountable&) noexcept
      : SingletonRelaxedCountable() {}
  SingletonRelaxedCountable(SingletonRelaxedCountable&&) noexcept
      : SingletonRelaxedCountable() {}

  SingletonRelaxedCountable& operator=(const SingletonRelaxedCountable&) =
      default;
  SingletonRelaxedCountable& operator=(SingletonRelaxedCountable&&) = default;

 private:
  friend class SingletonRelaxedCountableAccess<Counted>;

  struct Tag;
  using Counter = SingletonRelaxedCounter<size_t, Tag>;
};

//  SingletonRelaxedCountableAccess
//
//  Provides access to the running count of instances of a type using the CRTP
//  base class SingletonRelaxedCountable.
template <typename Counted>
class SingletonRelaxedCountableAccess {
 public:
  static size_t count() {
    return SingletonRelaxedCountable<Counted>::Counter::count();
  }
};

} // namespace folly
