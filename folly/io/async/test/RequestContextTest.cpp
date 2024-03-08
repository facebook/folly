/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <atomic>
#include <cstdint>
#include <thread>

#include <folly/Memory.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/Request.h>
#include <folly/io/async/test/RequestContextHelper.h>
#include <folly/portability/GTest.h>
#include <folly/system/ThreadName.h>

#include <boost/thread/barrier.hpp>

using namespace folly;

RequestToken testtoken("test");

class RequestContextTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Make sure each test starts out using the default context, and not some
    // other context left over by a previous test.
    RequestContext::setContext(nullptr);

    // Make sure no data is set for the "test" key when we start.  There could
    // be left over data in the default context from a previous test.  If we
    // don't clear it out future calls to setContextData() won't actually work,
    // and will reset the data to null instead of properly setting the new
    // desired data.
    //
    // (All of the tests generally want the behavior of overwriteContextData()
    // rather than setContextData(), but that method is private.)
    //
    // We ideally want to clear out data for any keys that may be set, not just
    // the "test" key, but there also isn't a RequestContext API to do this.
    clearData();
  }

  RequestContext& getContext() {
    auto* ctx = RequestContext::get();
    EXPECT_TRUE(ctx != nullptr);
    return *ctx;
  }

  void setData(int data = 0, std::string key = "test") {
    getContext().setContextData(key, std::make_unique<TestData>(data));
  }

  bool hasData(std::string key = "test") {
    return getContext().hasContextData(key);
  }

  const TestData& getData(std::string key = "test") {
    auto* ptr = dynamic_cast<TestData*>(getContext().getContextData(key));
    EXPECT_TRUE(ptr != nullptr);
    return *ptr;
  }

  void clearData(std::string key = "test") {
    getContext().clearContextData(key);
  }

  std::vector<intptr_t> getRootIdsFromAllThreads() {
    auto rootids = RequestContext::getRootIdsFromAllThreads();
    std::vector<intptr_t> result;
    std::transform(
        rootids.begin(), rootids.end(), std::back_inserter(result), [](auto e) {
          return e.id;
        });
    return result;
  }
};

TEST_F(RequestContextTest, SimpleTest) {
  EventBase base;

  // There should always be a default context with get()
  EXPECT_TRUE(RequestContext::get() != nullptr);
  // but fallback context should have rootid set to 0
  EXPECT_EQ(RequestContext::get()->getRootId(), 0);

  // but not with saveContext()
  EXPECT_EQ(RequestContext::saveContext(), nullptr);
  RequestContext::create();
  EXPECT_NE(RequestContext::saveContext(), nullptr);
  auto rootids = getRootIdsFromAllThreads();
  EXPECT_EQ(1, rootids.size());
  EXPECT_EQ(RequestContext::get()->getRootId(), rootids[0]);
  EXPECT_EQ(reinterpret_cast<intptr_t>(RequestContext::get()), rootids[0]);
  EXPECT_NE(RequestContext::get()->getRootId(), 0);
  RequestContext::create();
  EXPECT_NE(RequestContext::saveContext(), nullptr);
  EXPECT_NE(RequestContext::get()->getRootId(), rootids[0]);

  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));

  RequestContext::get()->setContextData("test", std::make_unique<TestData>(10));
  base.runInEventBaseThread([&]() {
    EXPECT_TRUE(RequestContext::get() != nullptr);
    auto data = dynamic_cast<TestData*>(
                    RequestContext::get()->getContextData(testtoken))
                    ->data_;
    EXPECT_EQ(10, data);
    rootids = getRootIdsFromAllThreads();
    EXPECT_EQ(2, rootids.size());
    EXPECT_EQ(RequestContext::get()->getRootId(), rootids[0]);
    EXPECT_EQ(RequestContext::get()->getRootId(), rootids[1]);
    base.terminateLoopSoon();
  });
  auto th = std::thread([&]() { base.loopForever(); });
  th.join();
  EXPECT_TRUE(RequestContext::get() != nullptr);
  auto a =
      dynamic_cast<TestData*>(RequestContext::get()->getContextData("test"));
  auto data = a->data_;
  EXPECT_EQ(10, data);

  RequestContext::setContext(std::shared_ptr<RequestContext>());
  // There should always be a default context
  EXPECT_TRUE(nullptr != RequestContext::get());
}

TEST_F(RequestContextTest, RequestContextScopeGuard) {
  RequestContextScopeGuard g0;
  setData(10);
  {
    RequestContextScopeGuard g1;
    EXPECT_FALSE(hasData());
    setData(20);
    EXPECT_EQ(20, getData().data_);
    EXPECT_EQ(1, getData().set_);
    EXPECT_EQ(0, getData().unset_);
  }
  EXPECT_EQ(10, getData().data_);
  EXPECT_EQ(2, getData().set_);
  EXPECT_EQ(1, getData().unset_);
}

TEST_F(RequestContextTest, defaultContext) {
  // Don't create a top level guard
  setData(10);
  {
    RequestContextScopeGuard g1;
    EXPECT_FALSE(hasData());
  }
  EXPECT_EQ(10, getData().data_);
  EXPECT_EQ(1, getData().set_);
  EXPECT_EQ(0, getData().unset_);
}

TEST_F(RequestContextTest, setIfAbsentTest) {
  EXPECT_TRUE(RequestContext::get() != nullptr);

  RequestContext::get()->setContextData("test", std::make_unique<TestData>(10));
  EXPECT_FALSE(RequestContext::get()->setContextDataIfAbsent(
      "test", std::make_unique<TestData>(20)));
  EXPECT_EQ(
      10,
      dynamic_cast<TestData*>(RequestContext::get()->getContextData(testtoken))
          ->data_);

  EXPECT_TRUE(RequestContext::get()->setContextDataIfAbsent(
      "test2", std::make_unique<TestData>(20)));
  EXPECT_EQ(
      20,
      dynamic_cast<TestData*>(RequestContext::get()->getContextData("test2"))
          ->data_);

  RequestContext::setContext(std::shared_ptr<RequestContext>());
  EXPECT_TRUE(nullptr != RequestContext::get());
}

TEST_F(RequestContextTest, testSetUnset) {
  RequestContext::create();
  auto ctx1 = RequestContext::saveContext();
  ctx1->setContextData("test", std::make_unique<TestData>(10));
  auto testData1 = dynamic_cast<TestData*>(ctx1->getContextData("test"));

  // onSet called in setContextData
  EXPECT_EQ(1, testData1->set_);
  EXPECT_EQ(ctx1.get(), testData1->onSetRctx);

  // Override RequestContext
  RequestContext::create();
  auto ctx2 = RequestContext::saveContext();
  ctx2->setContextData(testtoken, std::make_unique<TestData>(20));
  auto testData2 = dynamic_cast<TestData*>(ctx2->getContextData(testtoken));

  // onSet called in setContextData
  EXPECT_EQ(1, testData2->set_);
  EXPECT_EQ(ctx2.get(), testData2->onSetRctx);

  // Check ctx1->onUnset was called
  EXPECT_EQ(1, testData1->unset_);
  EXPECT_EQ(ctx1.get(), testData1->onUnSetRctx);

  RequestContext::setContext(ctx1);
  EXPECT_EQ(2, testData1->set_);
  EXPECT_EQ(1, testData1->unset_);
  EXPECT_EQ(1, testData2->unset_);
  EXPECT_EQ(ctx1.get(), testData1->onSetRctx);
  EXPECT_EQ(ctx1.get(), testData1->onUnSetRctx);
  EXPECT_EQ(ctx2.get(), testData2->onUnSetRctx);

  RequestContext::setContext(ctx2);
  EXPECT_EQ(2, testData1->set_);
  EXPECT_EQ(2, testData1->unset_);
  EXPECT_EQ(2, testData2->set_);
  EXPECT_EQ(1, testData2->unset_);
}

TEST_F(RequestContextTest, deadlockTest) {
  class DeadlockTestData : public RequestData {
   public:
    explicit DeadlockTestData(const std::string& val) : val_(val) {}

    ~DeadlockTestData() override {
      RequestContext::get()->setContextData(
          val_, std::make_unique<TestData>(1));
    }

    bool hasCallback() override { return false; }

    std::string val_;
  };

  RequestContext::get()->setContextData(
      "test", std::make_unique<DeadlockTestData>("test1"));
  RequestContext::get()->clearContextData(testtoken);
}

// A common use case is to use set/unset to maintain a thread global
// Regression test to ensure that unset is always called before set
TEST_F(RequestContextTest, sharedGlobalTest) {
  static bool global = false;

  class GlobalTestData : public RequestData {
   public:
    void onSet() override {
      ASSERT_FALSE(global);
      global = true;
    }

    void onUnset() override {
      ASSERT_TRUE(global);
      global = false;
    }

    bool hasCallback() override { return true; }
  };

  intptr_t root = 0;
  {
    RequestContextScopeGuard g0;
    RequestContext::get()->setContextData(
        "test", std::make_unique<GlobalTestData>());
    auto root0 = RequestContext::saveContext().get()->getRootId();
    EXPECT_EQ(getRootIdsFromAllThreads()[0], root0);
    {
      RequestContextScopeGuard g1;
      RequestContext::get()->setContextData(
          "test", std::make_unique<GlobalTestData>());
      auto root1 = RequestContext::saveContext().get()->getRootId();
      EXPECT_EQ(getRootIdsFromAllThreads()[0], root1);
    }
    EXPECT_EQ(getRootIdsFromAllThreads()[0], root0);
  }
  EXPECT_EQ(getRootIdsFromAllThreads()[0], root);
}

TEST_F(RequestContextTest, ShallowCopyBasic) {
  ShallowCopyRequestContextScopeGuard g0;
  setData(123, "immutable");
  EXPECT_EQ(123, getData("immutable").data_);
  EXPECT_FALSE(hasData());
  EXPECT_EQ(0, getRootIdsFromAllThreads()[0]);

  {
    ShallowCopyRequestContextScopeGuard g1;
    EXPECT_EQ(123, getData("immutable").data_);
    setData(789);
    EXPECT_EQ(789, getData().data_);
    EXPECT_EQ(0, getRootIdsFromAllThreads()[0]);
  }

  EXPECT_FALSE(hasData());
  EXPECT_EQ(123, getData("immutable").data_);
  EXPECT_EQ(1, getData("immutable").set_);
  EXPECT_EQ(0, getData("immutable").unset_);
  EXPECT_EQ(0, getRootIdsFromAllThreads()[0]);
}

TEST_F(RequestContextTest, ShallowCopyOverwrite) {
  RequestContextScopeGuard g0;
  setData(123);
  EXPECT_EQ(123, getData().data_);
  auto rootid = RequestContext::get()->getRootId();
  EXPECT_EQ(rootid, getRootIdsFromAllThreads()[0]);
  {
    ShallowCopyRequestContextScopeGuard g1(
        "test", std::make_unique<TestData>(789));
    EXPECT_EQ(789, getData().data_);
    EXPECT_EQ(1, getData().set_);
    EXPECT_EQ(0, getData().unset_);
    // should have inherited parent's rootid
    EXPECT_EQ(rootid, getRootIdsFromAllThreads()[0]);

    {
      // rootId is preserved for shallow copies of shallow copies
      ShallowCopyRequestContextScopeGuard g2;
      EXPECT_EQ(rootid, getRootIdsFromAllThreads()[0]);
    }
    EXPECT_EQ(rootid, getRootIdsFromAllThreads()[0]);
  }
  EXPECT_EQ(123, getData().data_);
  EXPECT_EQ(2, getData().set_);
  EXPECT_EQ(1, getData().unset_);
  EXPECT_EQ(rootid, getRootIdsFromAllThreads()[0]);
}

TEST_F(RequestContextTest, ShallowCopyDefaultContext) {
  // Don't set global scope guard
  setData(123);
  EXPECT_EQ(123, getData().data_);
  {
    ShallowCopyRequestContextScopeGuard g1(
        "test", std::make_unique<TestData>(789));
    EXPECT_EQ(789, getData().data_);
  }
  EXPECT_EQ(123, getData().data_);
  EXPECT_EQ(1, getData().set_);
  EXPECT_EQ(0, getData().unset_);
}

TEST_F(RequestContextTest, ShallowCopyClear) {
  RequestContextScopeGuard g0;
  setData(123);
  EXPECT_EQ(123, getData().data_);
  {
    ShallowCopyRequestContextScopeGuard g1;
    EXPECT_EQ(123, getData().data_);
    clearData();
    setData(789);
    EXPECT_EQ(789, getData().data_);
  }
  EXPECT_EQ(123, getData().data_);
  EXPECT_EQ(2, getData().set_);
  EXPECT_EQ(1, getData().unset_);
}

TEST_F(RequestContextTest, ShallowCopyMulti) {
  RequestContextScopeGuard g0;
  setData(1, "test1");
  setData(2, "test2");
  EXPECT_EQ(1, getData("test1").data_);
  EXPECT_EQ(2, getData("test2").data_);
  {
    ShallowCopyRequestContextScopeGuard g1(
        RequestDataItem{"test1", std::make_unique<TestData>(2)},
        RequestDataItem{"test2", std::make_unique<TestData>(4)});

    EXPECT_EQ(2, getData("test1").data_);
    EXPECT_EQ(4, getData("test2").data_);
    clearData("test1");
    clearData("test2");
    setData(4, "test1");
    setData(8, "test2");
    EXPECT_EQ(4, getData("test1").data_);
    EXPECT_EQ(8, getData("test2").data_);
  }
  EXPECT_EQ(1, getData("test1").data_);
  EXPECT_EQ(2, getData("test2").data_);
}

TEST_F(RequestContextTest, RootIdOnCopy) {
  auto ctxBase = std::make_shared<RequestContext>(0xab);
  EXPECT_EQ(0xab, ctxBase->getRootId());
  {
    auto ctx = RequestContext::copyAsRoot(*ctxBase, 0xabc);
    EXPECT_EQ(0xabc, ctx->getRootId());
  }
  {
    auto ctx = RequestContext::copyAsChild(*ctxBase);
    EXPECT_EQ(0xab, ctx->getRootId());
  }
}

TEST_F(RequestContextTest, ThreadId) {
  folly::setThreadName("DummyThread");
  RequestContextScopeGuard g;
  auto ctxBase = std::make_shared<RequestContext>();
  auto rootids = RequestContext::getRootIdsFromAllThreads();
  EXPECT_EQ(*folly::getThreadName(rootids[0].tid), "DummyThread");
  EXPECT_EQ(rootids[0].tidOS, folly::getOSThreadID());

  EventBase base;
  base.runInEventBaseThread([&]() {
    RequestContextScopeGuard g_;
    folly::setThreadName("DummyThread2");
    rootids = RequestContext::getRootIdsFromAllThreads();
    base.terminateLoopSoon();
  });

  auto th = std::thread([&]() { base.loopForever(); });
  th.join();

  std::sort(rootids.begin(), rootids.end(), [](const auto& a, const auto& b) {
    auto aname = folly::getThreadName(a.tid);
    auto bname = folly::getThreadName(b.tid);
    return (aname ? *aname : "zzz") < (bname ? *bname : "zzz");
  });

  EXPECT_EQ(*folly::getThreadName(rootids[0].tid), "DummyThread");
  EXPECT_FALSE(folly::getThreadName(rootids[1].tid));
}

TEST_F(RequestContextTest, Clear) {
  struct Foo : public RequestData {
    bool& cleared;
    bool& deleted;
    Foo(bool& c, bool& d) : cleared(c), deleted(d) {}
    ~Foo() override {
      EXPECT_TRUE(cleared);
      deleted = true;
    }
    bool hasCallback() override { return false; }
    void onClear() override {
      EXPECT_FALSE(cleared);
      cleared = true;
    }
  };

  std::string key = "clear";
  {
    bool cleared = false;
    bool deleted = false;
    {
      RequestContextScopeGuard g;
      RequestContext::get()->setContextData(
          key, std::make_unique<Foo>(cleared, deleted));
      EXPECT_FALSE(cleared);
      RequestContext::get()->clearContextData(key);
      EXPECT_TRUE(cleared);
    }
    EXPECT_TRUE(deleted);
  }
  {
    bool cleared = false;
    bool deleted = false;
    {
      RequestContextScopeGuard g;
      RequestContext::get()->setContextData(
          key, std::make_unique<Foo>(cleared, deleted));
      EXPECT_FALSE(cleared);
      EXPECT_FALSE(deleted);
    }
    EXPECT_TRUE(cleared);
    EXPECT_TRUE(deleted);
  }
}

TEST_F(RequestContextTest, OverwriteNullData) {
  folly::ShallowCopyRequestContextScopeGuard g0("token", nullptr);
  {
    folly::ShallowCopyRequestContextScopeGuard g1(
        "token", std::make_unique<TestData>(0));
    EXPECT_NE(folly::RequestContext::get()->getContextData("token"), nullptr);
  }
}

TEST_F(RequestContextTest, ConcurrentDataRefRelease) {
  for (int i = 0; i < 100; ++i) {
    std::atomic<int> step{0};
    std::shared_ptr<folly::RequestContext> sp1;
    auto th1 = std::thread([&]() {
      folly::RequestContextScopeGuard g0; // Creates ctx0.
      setData(); // Creates data0 with one reference in ctx0.
      {
        folly::ShallowCopyRequestContextScopeGuard g1;
        // g1 created ctx1 with second reference to data0.
        EXPECT_NE(&getData(), nullptr);
        // Keep shared_ptr to ctx1 to pass to th2
        sp1 = folly::RequestContext::saveContext();
        step.store(1); // sp1 is ready.
        while (step.load() < 2)
          /* Wait for th2 to clear reference to data0. */;
      }
      // End of g2 released shared_ptr to ctx1, switched back to ctx0
      // At this point:
      // - One shared_ptr to ctx0, held by th1.
      // - One shared_ptr to ctx1, help by th2.
      // - data0 has one clear count (for reference from ctx0) and
      //   two delete counts (one each from ctx0 and ctx1).
      step.store(3);
      // End of g1 will destroy ctx0, release clear/delete counts for data0.
    });
    auto th2 = std::thread([&]() {
      while (step.load() < 1)
        /* Wait for th1 to set sp1. */;
      folly::RequestContextScopeGuard g2(std::move(sp1));
      // g2 set context to ctx1.
      EXPECT_EQ(sp1.get(), nullptr);
      EXPECT_NE(&getData(), nullptr);
      clearData();
      step.store(2); // th2 cleared reference to data0 in ctx1.
      while (step.load() < 3)
        /* Wait for th1 to release shared_ptr to ctx1. */;
      // End of g2 will destroy ctx1, release delete count for data0.
    });
    th1.join();
    th2.join();
  }
}

TEST_F(RequestContextTest, AccessAllThreadsDestructionGuard) {
  constexpr auto kNumThreads = 128;

  std::vector<std::thread> threads{kNumThreads};
  boost::barrier barrier{kNumThreads + 1};

  std::atomic<std::size_t> count{0};
  for (auto& thread : threads) {
    thread = std::thread([&] {
      // Force creation of thread local
      RequestContext::get();
      ++count;
      // Wait for all other threads to do the same
      barrier.wait();
      // Wait until signaled to die
      barrier.wait();
    });
  }

  barrier.wait();
  // Sanity check
  EXPECT_EQ(count.load(), kNumThreads);

  {
    auto accessor = RequestContext::accessAllThreads();
    // Allow threads to die (but they should not as long as we hold accessor!)
    barrier.wait();
    auto accessorsCount = std::distance(accessor.begin(), accessor.end());
    EXPECT_EQ(accessorsCount, kNumThreads + 1);
    for (RequestContext::StaticContext& staticContext : accessor) {
      EXPECT_EQ(staticContext.requestContext, nullptr);
    }
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

namespace {

struct KeyATraits {
  static inline const RequestToken kToken{"keyA"};
};

struct KeyBTraits {
  static inline const RequestToken kToken{"keyB"};
};

} // namespace

TEST_F(RequestContextTest, GetThreadCachedContextData) {
  auto makeData = [](int value) {
    return std::make_unique<ImmutableRequestData<int>>(value);
  };

  auto getData = [](auto traits) {
    auto* data = RequestContext::try_get()
                     ->getThreadCachedContextData<decltype(traits)>();
    CHECK(data != nullptr);
    auto* idata = dynamic_cast<ImmutableRequestData<int>*>(data);
    CHECK(idata != nullptr);
    return idata;
  };

  RequestContextScopeGuard guard;

  RequestContext::try_get()->setContextData(KeyATraits::kToken, makeData(1));
  RequestContext::try_get()->setContextData(KeyBTraits::kToken, makeData(2));

  EXPECT_EQ(getData(KeyATraits{})->value(), 1);
  EXPECT_EQ(getData(KeyBTraits{})->value(), 2);

  RequestContext::try_get()->overwriteContextData(
      KeyATraits::kToken, makeData(3));
  EXPECT_EQ(getData(KeyATraits{})->value(), 3);
  EXPECT_EQ(getData(KeyBTraits{})->value(), 2);

  RequestContext::try_get()->clearContextData(KeyATraits::kToken);
  EXPECT_TRUE(
      RequestContext::try_get()->getThreadCachedContextData<KeyATraits>() ==
      nullptr);
  EXPECT_EQ(getData(KeyBTraits{})->value(), 2);

  // Invalidations are delivered from other threads too.
  std::thread([&, ctx = RequestContext::saveContext()] {
    RequestContextScopeGuard guard2(ctx);
    RequestContext::try_get()->setContextData(KeyATraits::kToken, makeData(4));
  }).join();
  EXPECT_EQ(getData(KeyATraits{})->value(), 4);
  EXPECT_EQ(getData(KeyBTraits{})->value(), 2);

  // Caches are not leaked when switching request context.
  {
    RequestContextScopeGuard guard3;
    EXPECT_TRUE(
        RequestContext::try_get()->getThreadCachedContextData<KeyATraits>() ==
        nullptr);
    EXPECT_TRUE(
        RequestContext::try_get()->getThreadCachedContextData<KeyBTraits>() ==
        nullptr);
  }
}

TEST(RequestContextTryGetTest, TryGetTest) {
  // try_get() should not create a default RequestContext object if none exists.
  EXPECT_EQ(RequestContext::try_get(), nullptr);
  // Explicitly create a new instance so that subsequent calls to try_get()
  // return it.
  RequestContext::create();
  EXPECT_NE(RequestContext::saveContext(), nullptr);
  EXPECT_NE(RequestContext::try_get(), nullptr);
  // Make sure that the pointers returned by both get() and try_get() point to
  // the same underlying instance.
  EXPECT_EQ(RequestContext::try_get(), RequestContext::get());
  // Set some context data and read it out via try_get() accessor.
  RequestContext::get()->setContextData("test", std::make_unique<TestData>(10));
  auto rc = RequestContext::try_get();
  EXPECT_TRUE(rc->hasContextData("test"));
  auto* dataPtr = dynamic_cast<TestData*>(rc->getContextData("test"));
  EXPECT_EQ(dataPtr->data_, 10);

  auto thread = std::thread([&] {
    auto accessor = RequestContext::accessAllThreads();
    // test there is no deadlock with try_get()
    RequestContext::try_get();
  });
  thread.join();

  thread = std::thread([&] {
    RequestContext::get();
    auto accessor = RequestContext::accessAllThreads();
    // test there is no deadlock with get()
    RequestContext::get();
  });
  thread.join();
}

TEST(ImmutableRequestTest, simple) {
  ImmutableRequestData<int> ird(4);
  EXPECT_EQ(ird.value(), 4);
}

TEST(ImmutableRequestTest, typeTraits) {
  using IRDI = ImmutableRequestData<int>;

  auto c1 = std::is_constructible<IRDI, int>::value;
  EXPECT_TRUE(c1);
  auto n1 = std::is_nothrow_constructible<IRDI, int>::value;
  EXPECT_TRUE(n1);

  auto c2 = std::is_constructible<IRDI, int, int>::value;
  EXPECT_FALSE(c2);
}
