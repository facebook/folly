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

#include <future>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/PrimaryPtr.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace std::literals::chrono_literals;

TEST(PrimaryPtrTest, Basic) {
  EXPECT_TRUE(folly::is_cleanup_v<folly::PrimaryPtr<int>>);

  auto ptr = std::make_unique<int>(42);
  auto rawPtr = ptr.get();

  folly::PrimaryPtr<int> primaryPtr(std::move(ptr));
  auto primaryPtrRef = primaryPtr.ref();
  EXPECT_TRUE(!!primaryPtr);

  auto lockedPtr1 = primaryPtr.lock();
  auto lockedPtr2 = primaryPtrRef.lock();
  EXPECT_EQ(lockedPtr1.get(), rawPtr);
  EXPECT_EQ(lockedPtr2.get(), rawPtr);

  EXPECT_EQ(lockedPtr1.use_count(), 3);
  EXPECT_EQ(lockedPtr2.use_count(), 3);

  EXPECT_TRUE(!!primaryPtr);

  auto joinFuture = std::async(std::launch::async, [&] {
    primaryPtr.join();
    EXPECT_TRUE(!primaryPtr);
  });

  auto lockFailFuture = std::async(std::launch::async, [&] {
    while (primaryPtr.lock()) {
      std::this_thread::yield();
    }
  });

  EXPECT_EQ(
      lockFailFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::ready);

  EXPECT_EQ(lockedPtr1.use_count(), 2);
  EXPECT_EQ(lockedPtr2.use_count(), 2);

  EXPECT_EQ(primaryPtr.lock().get(), nullptr);
  EXPECT_EQ(primaryPtrRef.lock().get(), nullptr);

  EXPECT_EQ(
      joinFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::timeout);

  lockedPtr1.reset();
  lockedPtr2.reset();

  EXPECT_EQ(
      joinFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::ready);

  EXPECT_TRUE(!primaryPtr);

  ptr = std::make_unique<int>(42);
  rawPtr = ptr.get();
  primaryPtr.set(std::move(ptr));
  EXPECT_TRUE(!!primaryPtr);
  lockedPtr1 = primaryPtr.lock();
  EXPECT_EQ(lockedPtr1.get(), rawPtr);
  lockedPtr1.reset();
  primaryPtr.join();
  EXPECT_EQ(primaryPtr.lock().get(), nullptr);
  EXPECT_TRUE(!primaryPtr);
}

struct Primed : folly::Cleanup, folly::EnablePrimaryFromThis<Primed> {
  folly::PrimaryPtr<int> nested_;
  folly::CPUThreadPoolExecutor pool_;
  Primed() : nested_(std::make_unique<int>(42)), pool_(4) {
    addCleanup(nested_);
    addCleanup(
        folly::makeSemiFuture().defer([this](auto&&) { this->pool_.join(); }));
  }
  using folly::Cleanup::addCleanup;
  std::shared_ptr<Primed> get_shared() {
    return masterLockFromThis();
  }
};

TEST(PrimaryPtrTest, BasicCleanup) {
  auto ptr = std::make_unique<Primed>();

  folly::PrimaryPtr<Primed> primaryPtr(std::move(ptr));
  int phase = 0;
  int index = 0;

  primaryPtr.lock()->addCleanup(
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      }));
  primaryPtr.lock()->addCleanup(
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      }));
  EXPECT_EQ(index, 2);

  folly::ManualExecutor exec;
  phase = 1;
  primaryPtr.cleanup()
      .within(1s)
      .via(folly::getKeepAliveToken(exec))
      .getVia(&exec);
  phase = 2;
  EXPECT_EQ(index, 0);
}

#if defined(__has_feature)
#if !__has_feature(address_sanitizer)
TEST(PrimaryPtrTest, Errors) {
  auto ptr = std::make_unique<Primed>();

  auto primaryPtr = std::make_unique<folly::PrimaryPtr<Primed>>(std::move(ptr));

  primaryPtr->lock()->addCleanup(folly::makeSemiFuture().deferValue(
      [](folly::Unit) { EXPECT_TRUE(false); }));

  primaryPtr->lock()->addCleanup(
      folly::makeSemiFuture<folly::Unit>(std::runtime_error("failed cleanup")));

  EXPECT_EXIT(
      primaryPtr->set(std::unique_ptr<Primed>{}),
      testing::KilledBySignal(SIGABRT),
      ".*joined before.*");

  folly::ManualExecutor exec;
  EXPECT_EXIT(
      primaryPtr->cleanup()
          .within(1s)
          .via(folly::getKeepAliveToken(exec))
          .getVia(&exec),
      testing::KilledBySignal(SIGABRT),
      ".*noexcept.*");

  EXPECT_EXIT(
      primaryPtr.reset(), testing::KilledBySignal(SIGABRT), ".*PrimaryPtr.*");

  // must leak the PrimaryPtr as its destructor will abort.
  (void)primaryPtr.release();
}
#endif
#endif

TEST(PrimaryPtrTest, Invariants) {
  struct BadDerived : Primed {
    ~BadDerived() {
      EXPECT_EXIT(
          addCleanup(folly::makeSemiFuture().deferValue(
              [](folly::Unit) { EXPECT_TRUE(false); })),
          testing::KilledBySignal(SIGABRT),
          ".*addCleanup.*");

      EXPECT_EXIT(
          addCleanup(folly::makeSemiFuture().deferValue(
              [](folly::Unit) { EXPECT_TRUE(false); })),
          testing::KilledBySignal(SIGABRT),
          ".*addCleanup.*");
    }
  };
  auto ptr = std::make_unique<BadDerived>();

  folly::PrimaryPtr<Primed> primaryPtr(std::move(ptr));

  auto ranCleanup = false;
  primaryPtr.lock()->addCleanup(folly::makeSemiFuture().deferValue(
      [&](folly::Unit) { ranCleanup = true; }));

  EXPECT_FALSE(ranCleanup);

  {
    folly::ManualExecutor exec;
    primaryPtr.cleanup()
        .within(1s)
        .via(folly::getKeepAliveToken(exec))
        .getVia(&exec);
  }

  EXPECT_TRUE(ranCleanup);

  {
    folly::ManualExecutor exec;
    EXPECT_EXIT(
        primaryPtr.cleanup().via(folly::getKeepAliveToken(exec)).getVia(&exec),
        testing::KilledBySignal(SIGABRT),
        ".*already.*");
  }
}

struct Derived : Primed {};

TEST(PrimaryPtrTest, EnablePrimaryFromThis) {
  auto ptr = std::make_unique<Derived>();
  auto rawPtr = ptr.get();

  auto primaryPtr = folly::PrimaryPtr<Primed>{std::move(ptr)};
  auto primaryPtrRef = primaryPtr.ref();

  auto lockedPtr1 = primaryPtr.lock();
  auto lockedPtr2 = primaryPtrRef.lock();
  EXPECT_EQ(lockedPtr1.get(), rawPtr);
  EXPECT_EQ(lockedPtr2.get(), rawPtr);

  EXPECT_EQ(lockedPtr1.use_count(), 3);
  EXPECT_EQ(lockedPtr2.use_count(), 3);

  auto lockedPtr3 = lockedPtr1->get_shared();
  EXPECT_EQ(lockedPtr3.use_count(), 4);
  EXPECT_EQ(lockedPtr3.get(), rawPtr);

  auto cleanupFuture = std::async(std::launch::async, [&] {
    folly::ManualExecutor exec;
    primaryPtr.cleanup()
        .within(1s)
        .via(folly::getKeepAliveToken(exec))
        .getVia(&exec);
    EXPECT_TRUE(!primaryPtr);
  });

  EXPECT_EQ(
      cleanupFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::timeout);

  EXPECT_EQ(lockedPtr1.use_count(), 3);
  EXPECT_EQ(lockedPtr2.use_count(), 3);
  EXPECT_EQ(lockedPtr3.use_count(), 3);

  EXPECT_EQ(primaryPtr.lock().get(), nullptr);
  EXPECT_EQ(primaryPtrRef.lock().get(), nullptr);

  EXPECT_EQ(
      cleanupFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::timeout);

  lockedPtr1.reset();
  lockedPtr2.reset();
  lockedPtr3.reset();

  EXPECT_EQ(
      cleanupFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::ready);

  EXPECT_TRUE(!primaryPtr);
}
