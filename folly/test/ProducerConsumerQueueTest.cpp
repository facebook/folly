/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/ProducerConsumerQueue.h"

#include <gtest/gtest.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <glog/logging.h>

//////////////////////////////////////////////////////////////////////

namespace {

template<class T> struct TestTraits {
  T limit() const { return 1 << 24; }
  T generate() const { return rand() % 26; }
};

template<> struct TestTraits<std::string> {
  int limit() const { return 1 << 21; }
  std::string generate() const { return std::string(12, ' '); }
};

template<class QueueType, size_t Size>
struct PerfTest {
  typedef typename QueueType::value_type T;

  explicit PerfTest() : queue_(Size), done_(false) {}

  void operator()() {
    using namespace std::chrono;
    auto const startTime = system_clock::now();

    std::thread producer([this] { this->producer(); });
    std::thread consumer([this] { this->consumer(); });

    producer.join();
    done_ = true;
    consumer.join();

    auto duration = duration_cast<milliseconds>(
      system_clock::now() - startTime);
    LOG(INFO) << "     done: " << duration.count() << "ms";
  }

  void producer() {
    for (int i = 0; i < traits_.limit(); ++i) {
      while (!queue_.write(traits_.generate())) {
      }
    }
  }

  void consumer() {
    while (!done_) {
      T data;
      queue_.read(data);
    }
  }

  QueueType queue_;
  std::atomic<bool> done_;
  TestTraits<T> traits_;
};

template<class TestType> void doTest(const char* name) {
  LOG(INFO) << "  testing: " << name;
  std::unique_ptr<TestType> const t(new TestType());
  (*t)();
}

template<class T> void perfTestType(const char* type) {
  const size_t size = 0xfffe;

  LOG(INFO) << "Type: " << type;
  doTest<PerfTest<folly::ProducerConsumerQueue<T>,size> >(
    "ProducerConsumerQueue");
}

template<class QueueType, size_t Size>
struct CorrectnessTest {
  typedef typename QueueType::value_type T;

  explicit CorrectnessTest()
    : queue_(Size)
    , done_(false)
  {
    const size_t testSize = traits_.limit();
    testData_.reserve(testSize);
    for (int i = 0; i < testSize; ++i) {
      testData_.push_back(traits_.generate());
    }
  }

  void operator()() {
    std::thread producer([this] { this->producer(); });
    std::thread consumer([this] { this->consumer(); });

    producer.join();
    done_ = true;
    consumer.join();
  }

  void producer() {
    for (auto& data : testData_) {
      while (!queue_.write(data)) {
      }
    }
  }

  void consumer() {
    for (auto& expect : testData_) {
    again:
      T data;
      if (!queue_.read(data)) {
        if (done_) {
          // Try one more read; unless there's a bug in the queue class
          // there should still be more data sitting in the queue even
          // though the producer thread exited.
          if (!queue_.read(data)) {
            EXPECT_TRUE(0 && "Finished too early ...");
            return;
          }
        } else {
          goto again;
        }
      }
      EXPECT_EQ(data, expect);
    }
  }

  std::vector<T> testData_;
  QueueType queue_;
  TestTraits<T> traits_;
  std::atomic<bool> done_;
};

template<class T> void correctnessTestType(const std::string& type) {
  LOG(INFO) << "Type: " << type;
  doTest<CorrectnessTest<folly::ProducerConsumerQueue<T>,0xfffe> >(
    "ProducerConsumerQueue");
}

struct DtorChecker {
  static int numInstances;
  DtorChecker() { ++numInstances; }
  DtorChecker(const DtorChecker& o) { ++numInstances; }
  ~DtorChecker() { --numInstances; }
};

int DtorChecker::numInstances = 0;

}

//////////////////////////////////////////////////////////////////////

TEST(PCQ, QueueCorrectness) {
  correctnessTestType<std::string>("string");
  correctnessTestType<int>("int");
  correctnessTestType<unsigned long long>("unsigned long long");
}

TEST(PCQ, PerfTest) {
  perfTestType<std::string>("string");
  perfTestType<int>("int");
  perfTestType<unsigned long long>("unsigned long long");
}

TEST(PCQ, Destructor) {
  // Test that orphaned elements in a ProducerConsumerQueue are
  // destroyed.
  {
    folly::ProducerConsumerQueue<DtorChecker> queue(1024);
    for (int i = 0; i < 10; ++i) {
      EXPECT_TRUE(queue.write(DtorChecker()));
    }

    EXPECT_EQ(DtorChecker::numInstances, 10);

    {
      DtorChecker ignore;
      EXPECT_TRUE(queue.read(ignore));
      EXPECT_TRUE(queue.read(ignore));
    }

    EXPECT_EQ(DtorChecker::numInstances, 8);
  }

  EXPECT_EQ(DtorChecker::numInstances, 0);

  // Test the same thing in the case that the queue write pointer has
  // wrapped, but the read one hasn't.
  {
    folly::ProducerConsumerQueue<DtorChecker> queue(4);
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(queue.write(DtorChecker()));
    }
    EXPECT_EQ(DtorChecker::numInstances, 3);
    {
      DtorChecker ignore;
      EXPECT_TRUE(queue.read(ignore));
    }
    EXPECT_EQ(DtorChecker::numInstances, 2);
    EXPECT_TRUE(queue.write(DtorChecker()));
    EXPECT_EQ(DtorChecker::numInstances, 3);
  }
  EXPECT_EQ(DtorChecker::numInstances, 0);
}
