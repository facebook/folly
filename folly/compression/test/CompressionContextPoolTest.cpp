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

#include <thread>

#include <folly/portability/GTest.h>

#include <folly/compression/CompressionContextPool.h>
#include <folly/compression/CompressionContextPoolSingletons.h>
#include <folly/compression/CompressionCoreLocalContextPool.h>

namespace folly {
namespace compression {

namespace {

std::atomic<size_t> numFoos{0};
std::atomic<size_t> numDeleted{0};

class Foo {};

struct FooCreator {
  Foo* operator()() {
    numFoos++;
    return new Foo();
  }
};

struct BadFooCreator {
  Foo* operator()() {
    numFoos++;
    return nullptr;
  }
};

struct FooDeleter {
  void operator()(Foo* f) {
    numDeleted++;
    delete f;
  }
};

using Pool = CompressionContextPool<Foo, FooCreator, FooDeleter>;
using BadPool = CompressionContextPool<Foo, BadFooCreator, FooDeleter>;

} // anonymous namespace

class CompressionContextPoolTest : public testing::Test {
 protected:
  void SetUp() override {
    pool_ = std::make_unique<Pool>();
  }

  void TearDown() override {
    pool_.reset();
  }

  std::unique_ptr<Pool> pool_;
};

TEST_F(CompressionContextPoolTest, testGet) {
  auto ptr = pool_->get();
  EXPECT_TRUE(ptr);
}

TEST_F(CompressionContextPoolTest, testSame) {
  Pool::Object* tmp;
  {
    auto ptr = pool_->get();
    tmp = ptr.get();
  }
  {
    auto ptr = pool_->get();
    EXPECT_EQ(tmp, ptr.get());
  }
  EXPECT_EQ(pool_->size(), 1);
}

TEST_F(CompressionContextPoolTest, testFree) {
  EXPECT_EQ(pool_->size(), 0);
  {
    auto ptr = pool_->get();
    EXPECT_TRUE(ptr);
    EXPECT_EQ(pool_->size(), 0);
  }
  EXPECT_EQ(pool_->size(), 1);
  EXPECT_EQ(numFoos.load(), 1);
  EXPECT_EQ(numDeleted.load(), 0);
  pool_.reset();
  EXPECT_EQ(numFoos.load(), numDeleted.load());
}

TEST_F(CompressionContextPoolTest, testDifferent) {
  {
    auto ptr1 = pool_->get();
    auto ptr2 = pool_->get();
    EXPECT_NE(ptr1.get(), ptr2.get());
  }
  EXPECT_EQ(pool_->size(), 2);
}

TEST_F(CompressionContextPoolTest, testLifo) {
  auto ptr1 = pool_->get();
  auto ptr2 = pool_->get();
  auto ptr3 = pool_->get();
  auto t1 = ptr1.get();
  auto t2 = ptr2.get();
  auto t3 = ptr3.get();
  EXPECT_NE(t1, t2);
  EXPECT_NE(t1, t3);
  EXPECT_NE(t2, t3);

  ptr3.reset();
  ptr2.reset();
  ptr1.reset();

  ptr1 = pool_->get();
  EXPECT_EQ(ptr1.get(), t1);
  ptr1.reset();

  ptr1 = pool_->get();
  EXPECT_EQ(ptr1.get(), t1);
  ptr2 = pool_->get();
  EXPECT_EQ(ptr2.get(), t2);
  ptr1.reset();
  ptr2.reset();

  ptr1 = pool_->get();
  EXPECT_EQ(ptr1.get(), t2);
  ptr2 = pool_->get();
  EXPECT_EQ(ptr2.get(), t1);
  ptr3 = pool_->get();
  EXPECT_EQ(ptr3.get(), t3);
}

TEST_F(CompressionContextPoolTest, testExplicitCreatorDeleter) {
  pool_ = std::make_unique<Pool>(FooCreator(), FooDeleter());
  auto ptr = pool_->get();
  EXPECT_TRUE(ptr);
}

TEST_F(CompressionContextPoolTest, testMultithread) {
  constexpr size_t numThreads = 64;
  constexpr size_t numIters = 1 << 14;
  std::vector<std::thread> ts;
  for (size_t i = 0; i < numThreads; i++) {
    ts.emplace_back([& pool = *pool_]() {
      for (size_t n = 0; n < numIters; n++) {
        auto ref = pool.get();
        ref.get();
        ref.reset();
      }
    });
  }

  for (auto& t : ts) {
    t.join();
  }

  EXPECT_LE(numFoos.load(), numThreads);
  EXPECT_LE(numDeleted.load(), 0);
}

TEST_F(CompressionContextPoolTest, testBadCreate) {
  BadPool pool;
  EXPECT_THROW(pool.get(), std::bad_alloc);
}

class CompressionCoreLocalContextPoolTest : public testing::Test {
 protected:
  using Pool = CompressionCoreLocalContextPool<Foo, FooCreator, FooDeleter, 8>;

  void SetUp() override {
    pool_ = std::make_unique<Pool>();
  }

  void TearDown() override {
    pool_.reset();
  }

  std::unique_ptr<Pool> pool_;
};

TEST_F(CompressionCoreLocalContextPoolTest, testGet) {
  auto ptr = pool_->get();
  EXPECT_TRUE(ptr);
}

TEST_F(CompressionCoreLocalContextPoolTest, testSame) {
  Pool::Object* tmp;
  {
    auto ptr = pool_->get();
    tmp = ptr.get();
  }
  {
    auto ptr = pool_->get();
    EXPECT_EQ(tmp, ptr.get());
  }
}

TEST_F(CompressionCoreLocalContextPoolTest, testDifferent) {
  auto ptr1 = pool_->get();
  auto ptr2 = pool_->get();
  EXPECT_NE(ptr1.get(), ptr2.get());
}

TEST_F(CompressionCoreLocalContextPoolTest, testSwap) {
  auto ptr1 = pool_->get();
  auto ptr2 = pool_->get();
  EXPECT_NE(ptr1.get(), ptr2.get());
  auto tmp1 = ptr1.get();
  auto tmp2 = ptr2.get();
  ptr2.reset();
  ptr1.reset();
  ptr2 = pool_->get();
  EXPECT_EQ(ptr2.get(), tmp2);
  ptr1 = pool_->get();
  EXPECT_EQ(ptr1.get(), tmp1);
  ptr1.reset();
  ptr2.reset();
  ptr1 = pool_->get();
  EXPECT_EQ(ptr1.get(), tmp1);
  ptr2 = pool_->get();
  EXPECT_EQ(ptr2.get(), tmp2);
}

TEST_F(CompressionCoreLocalContextPoolTest, testMultithread) {
  constexpr size_t numThreads = 64;
  constexpr size_t numIters = 1 << 14;
  std::vector<std::thread> ts;
  for (size_t i = 0; i < numThreads; i++) {
    ts.emplace_back([& pool = *pool_]() {
      for (size_t n = 0; n < numIters; n++) {
        auto ref = pool.get();
        CHECK(ref);
        ref.reset();
      }
    });
  }

  for (auto& t : ts) {
    t.join();
  }

  EXPECT_LE(numFoos.load(), numThreads);
}

#ifdef FOLLY_COMPRESSION_HAS_ZSTD_CONTEXT_POOL_SINGLETONS

TEST(CompressionContextPoolSingletonsTest, testSingletons) {
  EXPECT_NE(contexts::getZSTD_CCtx(), nullptr);
  EXPECT_NE(contexts::getZSTD_DCtx(), nullptr);
}

TEST(CompressionContextPoolSingletonsTest, testSingletonsNull) {
  EXPECT_EQ(contexts::getNULL_ZSTD_CCtx(), nullptr);
  EXPECT_EQ(contexts::getNULL_ZSTD_DCtx(), nullptr);
}

#endif // FOLLY_COMPRESSION_HAS_ZSTD_CONTEXT_POOL_SINGLETONS

} // namespace compression
} // namespace folly
