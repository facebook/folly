/*
 * Copyright 2014 Facebook, Inc.
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

#ifndef FOLLY_DETAIL_CACHELOCALITY_H_
#define FOLLY_DETAIL_CACHELOCALITY_H_

#include <sched.h>
#include <atomic>
#include <cassert>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include "folly/Likely.h"

namespace folly { namespace detail {

// This file contains several classes that might be useful if you are
// trying to dynamically optimize cache locality: CacheLocality reads
// cache sharing information from sysfs to determine how CPUs should be
// grouped to minimize contention, Getcpu provides fast access to the
// current CPU via __vdso_getcpu, and AccessSpreader uses these two to
// optimally spread accesses among a predetermined number of stripes.
//
// AccessSpreader<>::current(n) microbenchmarks at 22 nanos, which is
// substantially less than the cost of a cache miss.  This means that we
// can effectively use it to reduce cache line ping-pong on striped data
// structures such as IndexedMemPool or statistics counters.
//
// Because CacheLocality looks at all of the cache levels, it can be
// used for different levels of optimization.  AccessSpreader(2) does
// per-chip spreading on a dual socket system.  AccessSpreader(numCpus)
// does perfect per-cpu spreading.  AccessSpreader(numCpus / 2) does
// perfect L1 spreading in a system with hyperthreading enabled.

struct CacheLocality {

  /// 1 more than the maximum value that can be returned from sched_getcpu
  /// or getcpu.  This is the number of hardware thread contexts provided
  /// by the processors
  size_t numCpus;

  /// Holds the number of caches present at each cache level (0 is
  /// the closest to the cpu).  This is the number of AccessSpreader
  /// stripes needed to avoid cross-cache communication at the specified
  /// layer.  numCachesByLevel.front() is the number of L1 caches and
  /// numCachesByLevel.back() is the number of last-level caches.
  std::vector<size_t> numCachesByLevel;

  /// A map from cpu (from sched_getcpu or getcpu) to an index in the
  /// range 0..numCpus-1, where neighboring locality indices are more
  /// likely to share caches then indices far away.  All of the members
  /// of a particular cache level be contiguous in their locality index.
  /// For example, if numCpus is 32 and numCachesByLevel.back() is 2,
  /// then cpus with a locality index < 16 will share one last-level
  /// cache and cpus with a locality index >= 16 will share the other.
  std::vector<size_t> localityIndexByCpu;


  /// Returns the best CacheLocality information available for the current
  /// system, cached for fast access.  This will be loaded from sysfs if
  /// possible, otherwise it will be correct in the number of CPUs but
  /// not in their sharing structure.
  ///
  /// If you are into yo dawgs, this is a shared cache of the local
  /// locality of the shared caches.
  ///
  /// The template parameter here is used to allow injection of a
  /// repeatable CacheLocality structure during testing.  Rather than
  /// inject the type of the CacheLocality provider into every data type
  /// that transitively uses it, all components select between the default
  /// sysfs implementation and a deterministic implementation by keying
  /// off the type of the underlying atomic.  See DeterministicScheduler.
  template <template<typename> class Atom = std::atomic>
  static const CacheLocality& system();


  /// Reads CacheLocality information from a tree structured like
  /// the sysfs filesystem.  The provided function will be evaluated
  /// for each sysfs file that needs to be queried.  The function
  /// should return a string containing the first line of the file
  /// (not including the newline), or an empty string if the file does
  /// not exist.  The function will be called with paths of the form
  /// /sys/devices/system/cpu/cpu*/cache/index*/{type,shared_cpu_list} .
  /// Throws an exception if no caches can be parsed at all.
  static CacheLocality readFromSysfsTree(
      const std::function<std::string(std::string)>& mapping);

  /// Reads CacheLocality information from the real sysfs filesystem.
  /// Throws an exception if no cache information can be loaded.
  static CacheLocality readFromSysfs();

  /// Returns a usable (but probably not reflective of reality)
  /// CacheLocality structure with the specified number of cpus and a
  /// single cache level that associates one cpu per cache.
  static CacheLocality uniform(size_t numCpus);

  enum {
    /// Memory locations on the same cache line are subject to false
    /// sharing, which is very bad for performance.  Microbenchmarks
    /// indicate that pairs of cache lines also see interference under
    /// heavy use of atomic operations (observed for atomic increment on
    /// Sandy Bridge).  See FOLLY_ALIGN_TO_AVOID_FALSE_SHARING
    kFalseSharingRange = 128
  };

  static_assert(kFalseSharingRange == 128,
      "FOLLY_ALIGN_TO_AVOID_FALSE_SHARING should track kFalseSharingRange");
};

// TODO replace __attribute__ with alignas and 128 with kFalseSharingRange

/// An attribute that will cause a variable or field to be aligned so that
/// it doesn't have false sharing with anything at a smaller memory address.
#define FOLLY_ALIGN_TO_AVOID_FALSE_SHARING __attribute__((aligned(128)))

/// Holds a function pointer to the VDSO implementation of getcpu(2),
/// if available
struct Getcpu {
  /// Function pointer to a function with the same signature as getcpu(2).
  typedef int (*Func)(unsigned* cpu, unsigned* node, void* unused);

  /// Returns a pointer to the VDSO implementation of getcpu(2), if
  /// available, or nullptr otherwise
  static Func vdsoFunc();
};

/// A class that lazily binds a unique (for each implementation of Atom)
/// identifier to a thread.  This is a fallback mechanism for the access
/// spreader if we are in testing (using DeterministicAtomic) or if
/// __vdso_getcpu can't be dynamically loaded
template <template<typename> class Atom>
struct SequentialThreadId {

  /// Returns the thread id assigned to the current thread
  static size_t get() {
    auto rv = currentId;
    if (UNLIKELY(rv == 0)) {
      rv = currentId = ++prevId;
    }
    return rv;
  }

  /// Fills the thread id into the cpu and node out params (if they
  /// are non-null).  This method is intended to act like getcpu when a
  /// fast-enough form of getcpu isn't available or isn't desired
  static int getcpu(unsigned* cpu, unsigned* node, void* unused) {
    auto id = get();
    if (cpu) {
      *cpu = id;
    }
    if (node) {
      *node = id;
    }
    return 0;
  }

 private:
  static Atom<size_t> prevId;

  // TODO: switch to thread_local
  static __thread size_t currentId;
};

template <template<typename> class Atom, size_t kMaxCpus>
struct AccessSpreaderArray;

/// AccessSpreader arranges access to a striped data structure in such a
/// way that concurrently executing threads are likely to be accessing
/// different stripes.  It does NOT guarantee uncontended access.
/// Your underlying algorithm must be thread-safe without spreading, this
/// is merely an optimization.  AccessSpreader::current(n) is typically
/// much faster than a cache miss (22 nanos on my dev box, tested fast
/// in both 2.6 and 3.2 kernels).
///
/// You are free to create your own AccessSpreader-s or to cache the
/// results of AccessSpreader<>::shared(n), but you will probably want
/// to use one of the system-wide shared ones.  Calling .current() on
/// a particular AccessSpreader instance only saves about 1 nanosecond
/// over calling AccessSpreader<>::shared(n).
///
/// If available (and not using the deterministic testing implementation)
/// AccessSpreader uses the getcpu system call via VDSO and the
/// precise locality information retrieved from sysfs by CacheLocality.
/// This provides optimal anti-sharing at a fraction of the cost of a
/// cache miss.
///
/// When there are not as many stripes as processors, we try to optimally
/// place the cache sharing boundaries.  This means that if you have 2
/// stripes and run on a dual-socket system, your 2 stripes will each get
/// all of the cores from a single socket.  If you have 16 stripes on a
/// 16 core system plus hyperthreading (32 cpus), each core will get its
/// own stripe and there will be no cache sharing at all.
///
/// AccessSpreader has a fallback mechanism for when __vdso_getcpu can't be
/// loaded, or for use during deterministic testing.  Using sched_getcpu or
/// the getcpu syscall would negate the performance advantages of access
/// spreading, so we use a thread-local value and a shared atomic counter
/// to spread access out.
///
/// AccessSpreader is templated on the template type that is used
/// to implement atomics, as a way to instantiate the underlying
/// heuristics differently for production use and deterministic unit
/// testing.  See DeterministicScheduler for more.  If you aren't using
/// DeterministicScheduler, you can just use the default template parameter
/// all of the time.
template <template<typename> class Atom = std::atomic>
struct AccessSpreader {

  /// Returns a never-destructed shared AccessSpreader instance.
  /// numStripes should be > 0.
  static const AccessSpreader& shared(size_t numStripes) {
    // sharedInstances[0] actually has numStripes == 1
    assert(numStripes > 0);

    // the last shared element handles all large sizes
    return AccessSpreaderArray<Atom,kMaxCpus>::sharedInstance[
        std::min(size_t(kMaxCpus), numStripes)];
  }

  /// Returns the stripe associated with the current CPU, assuming
  /// that there are numStripes (non-zero) stripes.  Equivalent to
  /// AccessSpreader::shared(numStripes)->current.
  static size_t current(size_t numStripes) {
    return shared(numStripes).current();
  }

  /// stripeByCore uses 1 stripe per L1 cache, according to
  /// CacheLocality::system<>().  Use stripeByCore.numStripes() to see
  /// its width, or stripeByCore.current() to get the current stripe
  static const AccessSpreader stripeByCore;

  /// stripeByChip uses 1 stripe per last-level cache, which is the fewest
  /// number of stripes for which off-chip communication can be avoided
  /// (assuming all caches are on-chip).  Use stripeByChip.numStripes()
  /// to see its width, or stripeByChip.current() to get the current stripe
  static const AccessSpreader stripeByChip;


  /// Constructs an AccessSpreader that will return values from
  /// 0 to numStripes-1 (inclusive), precomputing the mapping
  /// from CPU to stripe.  There is no use in having more than
  /// CacheLocality::system<Atom>().localityIndexByCpu.size() stripes or
  /// kMaxCpus stripes
  explicit AccessSpreader(size_t spreaderNumStripes,
                          const CacheLocality& cacheLocality =
                              CacheLocality::system<Atom>(),
                          Getcpu::Func getcpuFunc = nullptr)
    : getcpuFunc_(getcpuFunc ? getcpuFunc : pickGetcpuFunc(spreaderNumStripes))
    , numStripes_(spreaderNumStripes)
  {
    auto n = cacheLocality.numCpus;
    for (size_t cpu = 0; cpu < kMaxCpus && cpu < n; ++cpu) {
      auto index = cacheLocality.localityIndexByCpu[cpu];
      assert(index < n);
      // as index goes from 0..n, post-transform value goes from
      // 0..numStripes
      stripeByCpu[cpu] = (index * numStripes_) / n;
      assert(stripeByCpu[cpu] < numStripes_);
    }
    for (size_t cpu = n; cpu < kMaxCpus; ++cpu) {
      stripeByCpu[cpu] = stripeByCpu[cpu - n];
    }
  }

  /// Returns 1 more than the maximum value that can be returned from
  /// current()
  size_t numStripes() const {
    return numStripes_;
  }

  /// Returns the stripe associated with the current CPU
  size_t current() const {
    unsigned cpu;
    getcpuFunc_(&cpu, nullptr, nullptr);
    return stripeByCpu[cpu % kMaxCpus];
  }

 private:

  /// If there are more cpus than this nothing will crash, but there
  /// might be unnecessary sharing
  enum { kMaxCpus = 128 };

  typedef uint8_t CompactStripe;

  static_assert((kMaxCpus & (kMaxCpus - 1)) == 0,
      "kMaxCpus should be a power of two so modulo is fast");
  static_assert(kMaxCpus - 1 <= std::numeric_limits<CompactStripe>::max(),
      "stripeByCpu element type isn't wide enough");


  /// Points to the getcpu-like function we are using to obtain the
  /// current cpu.  It should not be assumed that the returned cpu value
  /// is in range.  We use a member for this instead of a static so that
  /// this fetch preloads a prefix the stripeByCpu array
  Getcpu::Func getcpuFunc_;

  /// A precomputed map from cpu to stripe.  Rather than add a layer of
  /// indirection requiring a dynamic bounds check and another cache miss,
  /// we always precompute the whole array
  CompactStripe stripeByCpu[kMaxCpus];

  size_t numStripes_;

  /// Returns the best getcpu implementation for this type and width
  /// of AccessSpreader
  static Getcpu::Func pickGetcpuFunc(size_t numStripes);
};

/// An array of kMaxCpus+1 AccessSpreader<Atom> instances constructed
/// with default params, with the zero-th element having 1 stripe
template <template<typename> class Atom, size_t kMaxStripe>
struct AccessSpreaderArray {

  AccessSpreaderArray() {
    for (size_t i = 0; i <= kMaxStripe; ++i) {
      new (raw + i) AccessSpreader<Atom>(std::max(size_t(1), i));
    }
  }

  ~AccessSpreaderArray() {
    for (size_t i = 0; i <= kMaxStripe; ++i) {
      auto p = static_cast<AccessSpreader<Atom>*>(static_cast<void*>(raw + i));
      p->~AccessSpreader();
    }
  }

  AccessSpreader<Atom> const& operator[] (size_t index) const {
    return *static_cast<AccessSpreader<Atom> const*>(
        static_cast<void const*>(raw + index));
  }

 private:

  // AccessSpreader uses sharedInstance
  friend AccessSpreader<Atom>;

  static AccessSpreaderArray<Atom,kMaxStripe> sharedInstance;


  /// aligned_storage is uninitialized, we use placement new since there
  /// is no AccessSpreader default constructor
  typename std::aligned_storage<sizeof(AccessSpreader<Atom>),
                                CacheLocality::kFalseSharingRange>::type
      raw[kMaxStripe + 1];
};

} }

#endif /* FOLLY_DETAIL_CacheLocality_H_ */

