/*
 * Copyright 2015 Facebook, Inc.
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
/* -*- Mode: C++; tab-width: 2; c-basic-offset: 2; indent-tabs-mode: nil -*- */

#include <thread>
#include <iostream>
#include <folly/Benchmark.h>
#include <folly/Memory.h>
#include <gflags/gflags.h>

#include <folly/ReadMostlySharedPtr.h>

/**
 * @file Benchmark comparing three implementations of ReadMostlySharedPtr.
 *
 *       Run with something like --bm_min_usec=100000.
 */

namespace slow {

// An implementation with thread local cache of shared_ptrs.
template<typename T>
class ReadMostlySharedPtr : boost::noncopyable {
 public:
  explicit ReadMostlySharedPtr(std::shared_ptr<T> ptr = nullptr) {
    master_.ptr = std::move(ptr);
    master_.version.store(1);
  }

  std::shared_ptr<T> store(std::shared_ptr<T> ptr) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::swap(master_.ptr, ptr);
    master_.version.fetch_add(1);
    return ptr;
  }

  std::shared_ptr<T> load() const {
    // We are the only thread accessing threadLocalCache_->version so it is
    // fine to use memory_order_relaxed
    auto local_version =
      threadLocalCache_->version.load(std::memory_order_relaxed);
    if (local_version != master_.version.load()) {
      std::lock_guard<std::mutex> guard(mutex_);
      threadLocalCache_->ptr = master_.ptr;
      threadLocalCache_->version.store(master_.version.load(),
                                       std::memory_order_relaxed);
    }
    return threadLocalCache_->ptr;
  }

 private:
  struct VersionedPointer : boost::noncopyable {
    VersionedPointer() : version(0) { }
    std::shared_ptr<T> ptr;
    std::atomic<uint64_t> version;
  };

  folly::ThreadLocal<VersionedPointer> threadLocalCache_;
  VersionedPointer master_;

  // Ensures safety between concurrent store() and load() calls
  mutable std::mutex mutex_;
};

}


/**
 * At the moment the fastest implementation in this benchmark.
 * A real RCU implementation would most likely be significantly better.
 */
namespace fast {

/**
 * Contains a version number and a shared_ptr that points to the most recent
 * object. The load() method uses thread-local storage to efficiently return
 * the current pointer without locking when the pointer has not changed.
 * The version of the pointer in thread-local cache is compared to the
 * master version. If the master is found to be newer, it is copied into
 * the thread-local cache under a lock. The store() method grabs the lock,
 * updates the master pointer and bumps the version number.
 *
 * The downside is that it doesn't clear or update thread-local cache
 * when updating the pointer. This means that old instances of T can stay
 * alive in thread-local cache indefinitely if load() is not called from
 * some threads.
 */
template<typename T>
class ReadMostlySharedPtr : boost::noncopyable {
 public:
  explicit ReadMostlySharedPtr(std::shared_ptr<T> ptr = nullptr) {
    masterPtr_ = std::move(ptr);
    masterVersion_.store(1);
  }

  /**
   * Replaces the managed object.
   */
  void store(std::shared_ptr<T> ptr) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      // Swap to avoid calling ~T() under the lock
      std::swap(masterPtr_, ptr);
      masterVersion_.fetch_add(1);
    }
  }

  /**
   * Returns a shared_ptr to the managed object.
   */
  std::shared_ptr<T> load() const {
    auto& local = *threadLocalCache_;
    if (local.version != masterVersion_.load()) {
      std::lock_guard<std::mutex> guard(mutex_);

      if (!masterPtr_) {
        local.ptr = nullptr;
      } else {
        // The following expression is tricky.
        //
        // It creates a shared_ptr<shared_ptr<T>> that points to a copy of
        // masterPtr_. The reference counter of this shared_ptr<shared_ptr<T>>
        // will normally only be modified from this thread, which avoids
        // cache line bouncing. (Though the caller is free to pass the pointer
        // to other threads and bump reference counter from there)
        //
        // Then this shared_ptr<shared_ptr<T>> is turned into shared_ptr<T>.
        // This means that the returned shared_ptr<T> will internally point to
        // control block of the shared_ptr<shared_ptr<T>>, but will dereference
        // to T, not shared_ptr<T>.
        local.ptr = std::shared_ptr<T>(
          std::make_shared<std::shared_ptr<T>>(masterPtr_),
          masterPtr_.get());
      }

      local.version = masterVersion_.load();
    }
    return local.ptr;
  }

 private:
  struct VersionedPointer : boost::noncopyable {
    VersionedPointer() { }
    std::shared_ptr<T> ptr;
    uint64_t version = 0;
  };

  folly::ThreadLocal<VersionedPointer> threadLocalCache_;

  std::shared_ptr<T> masterPtr_;
  std::atomic<uint64_t> masterVersion_;

  // Ensures safety between concurrent store() and load() calls
  mutable std::mutex mutex_;
};

}


template<class PtrInt>
void benchReads(int n) {
  PtrInt ptr(folly::make_unique<int>(42));
  for (int i = 0; i < n; ++i) {
    auto val = ptr.load();
    folly::doNotOptimizeAway(val.get());
  }
}

template<class PtrInt>
void benchWrites(int n) {
  PtrInt ptr;
  for (int i = 0; i < n; ++i) {
    ptr.store(folly::make_unique<int>(3));
  }
}

template<class PtrInt>
void benchReadsWhenWriting(int n) {
  PtrInt ptr;
  std::atomic<bool> shutdown {false};
  std::thread writing_thread;

  BENCHMARK_SUSPEND {
    writing_thread = std::thread([&] {
      for (uint64_t i = 0; !shutdown.load(); ++i) {
        ptr.store(folly::make_unique<int>(3));
      }
    });
  }

  for (int i = 0; i < n; ++i) {
    auto val = ptr.load();
    folly::doNotOptimizeAway(val.get());
  }

  BENCHMARK_SUSPEND {
    shutdown.store(true);
    writing_thread.join();
  }
}


template<class PtrInt>
void benchWritesWhenReading(int n) {
  PtrInt ptr;
  std::atomic<bool> shutdown {false};
  std::thread reading_thread;

  BENCHMARK_SUSPEND {
    reading_thread = std::thread([&] {
      for (uint64_t i = 0; !shutdown.load(); ++i) {
        auto val = ptr.load();
        folly::doNotOptimizeAway(val.get());
      }
    });
  }


  for (int i = 0; i < n; ++i) {
    ptr.store(folly::make_unique<int>(3));
  }

  BENCHMARK_SUSPEND {
    shutdown.store(true);
    reading_thread.join();
  }
}


template<class PtrInt>
void benchReadsIn10Threads(int n) {
  PtrInt ptr(folly::make_unique<int>(27));
  std::vector<std::thread> threads(10);
  int n_per_thread = n;

  for (std::thread& t: threads) {
    t = std::thread([&] {
      for (int i = 0; i < n; ++i) {
        auto val = ptr.load();
        folly::doNotOptimizeAway(val.get());
      }
    });
  }

  for (std::thread& t: threads) {
    t.join();
  }
}


#define BENCH(name)                                               \
  BENCHMARK(name ## _Slow, n) {                                   \
    bench ## name <slow::ReadMostlySharedPtr<int>>(n);      \
  }                                                               \
  BENCHMARK(name ## _ReadMostlySharedPtr, n) {              \
    bench ## name <folly::ReadMostlySharedPtr<int, int>>(n);\
  }                                                               \
  BENCHMARK(name ## _FastReadMostlySharedPtr, n) {          \
    bench ## name <fast::ReadMostlySharedPtr<int>>(n);      \
  }                                                               \
  BENCHMARK_DRAW_LINE();


BENCH(Reads)
BENCH(Writes)
BENCH(ReadsWhenWriting)
BENCH(WritesWhenReading)
BENCH(ReadsIn10Threads)

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
    "bm_min_usec", "100000", gflags::SET_FLAG_IF_DEFAULT
  );

  folly::runBenchmarks();

  return 0;
}

/*
============================================================================
folly/test/ReadMostlySharedPtrBenchmark.cpp     relative  time/iter  iters/s
============================================================================
Reads_Slow                                                  21.05ns   47.52M
Reads_ReadMostlySharedPtr                                   30.57ns   32.71M
Reads_FastReadMostlySharedPtr                               21.24ns   47.09M
----------------------------------------------------------------------------
Writes_Slow                                                117.52ns    8.51M
Writes_ReadMostlySharedPtr                                 145.26ns    6.88M
Writes_FastReadMostlySharedPtr                             116.26ns    8.60M
----------------------------------------------------------------------------
ReadsWhenWriting_Slow                                       56.18ns   17.80M
ReadsWhenWriting_ReadMostlySharedPtr                       141.32ns    7.08M
ReadsWhenWriting_FastReadMostlySharedPtr                    51.82ns   19.30M
----------------------------------------------------------------------------
WritesWhenReading_Slow                                     828.32ns    1.21M
WritesWhenReading_ReadMostlySharedPtr                        3.00us  333.63K
WritesWhenReading_FastReadMostlySharedPtr                  677.28ns    1.48M
----------------------------------------------------------------------------
ReadsIn10Threads_Slow                                      509.37ns    1.96M
ReadsIn10Threads_ReadMostlySharedPtr                        34.33ns   29.13M
ReadsIn10Threads_FastReadMostlySharedPtr                    26.31ns   38.00M
----------------------------------------------------------------------------
============================================================================
*/
