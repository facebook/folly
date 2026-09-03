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

#include <folly/tracing/AsyncStack.h>
#include <folly/tracing/test/AsyncStackTestUtils.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_set>
#include <glog/logging.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

TEST(AsyncStack, ScopedAsyncStackRoot) {
  void* const stackFramePtr = FOLLY_ASYNC_STACK_FRAME_POINTER();
  void* const returnAddress = FOLLY_ASYNC_STACK_RETURN_ADDRESS();

  CHECK(folly::tryGetCurrentAsyncStackRoot() == nullptr);

  {
    folly::detail::ScopedAsyncStackRoot scopedRoot{
        stackFramePtr, returnAddress};
    auto* root = folly::tryGetCurrentAsyncStackRoot();
    CHECK_NOTNULL(root);

    folly::AsyncStackFrame frame;
    scopedRoot.activateFrame(frame);

    CHECK_EQ(root, frame.getStackRoot());
    CHECK_EQ(stackFramePtr, root->getStackFramePointer());
    CHECK_EQ(returnAddress, root->getReturnAddress());
    CHECK_EQ(&frame, root->getTopFrame());

    folly::deactivateAsyncStackFrame(frame);

    CHECK(frame.getStackRoot() == nullptr);
    CHECK(root->getTopFrame() == nullptr);
  }

  CHECK(folly::tryGetCurrentAsyncStackRoot() == nullptr);
}

TEST(AsyncStack, MetadataValueSemantics) {
  // This type replaces `std::optional` in profiler output buffers, so it keeps
  // optional semantics, including zero as a present value.
  using Metadata = folly::AsyncStackMetadata;
  constexpr auto kZero = Metadata::value_type{0};
  constexpr auto kMax = std::numeric_limits<Metadata::value_type>::max();
  auto empty = Metadata{};
  auto null = Metadata{std::nullopt};
  auto zero = Metadata{kZero};
  auto max = Metadata{kMax};

  EXPECT_FALSE(empty.has_value());
  EXPECT_FALSE(static_cast<bool>(empty));
  EXPECT_EQ(empty, null);
  EXPECT_EQ(empty.get_pointer(), nullptr);
  EXPECT_EQ(empty.value_or(kMax), kMax);
  EXPECT_THROW(empty.value(), std::bad_optional_access);

  EXPECT_TRUE(static_cast<bool>(zero));
  EXPECT_EQ(zero.value(), kZero);
  EXPECT_EQ(*zero, kZero);
  EXPECT_THAT(zero.get_pointer(), testing::Pointee(kZero));
  EXPECT_EQ(zero.operator->(), zero.get_pointer());
  EXPECT_EQ(zero.value_or(kMax), kZero);
  EXPECT_EQ(max.value(), kMax);

  EXPECT_NE(null, zero);

  empty = kZero;
  EXPECT_EQ(empty, zero);
  empty = std::nullopt;
  EXPECT_EQ(empty, null);
  EXPECT_EQ(empty.emplace(kMax), kMax);
  EXPECT_EQ(empty, max);
  swap(empty, zero);
  EXPECT_EQ(empty.value(), kZero);
  EXPECT_EQ(zero.value(), kMax);
  zero.reset();
  EXPECT_FALSE(zero.has_value());
}

TEST(AsyncStack, MetadataFrameRecognition) {
  folly::AsyncStackFrame frame;
  EXPECT_FALSE(folly::detail::isAsyncStackMetadataFrame(frame));
  frame.setReturnAddress(&frame);
  EXPECT_FALSE(folly::detail::isAsyncStackMetadataFrame(frame));
#if FOLLY_HAS_ASYNC_STACK_METADATA
  folly::test::markAsMetadataFrame(frame);
  EXPECT_TRUE(folly::detail::isAsyncStackMetadataFrame(frame));
#endif
}

#if FOLLY_HAS_ASYNC_STACK_METADATA
TEST(AsyncStack, InitialFrameWalkOmitsMarkerButKeepsWrapper) {
  using namespace folly;
  using test::linkInactiveFrames;
  using test::markAsMetadataFrame;

  // `wrapper -> [marker] -> parent`: the marker uses no output slot, so the
  // full walk returns two addresses and leaves the spare slot untouched.
  AsyncStackFrame parentFrame;
  parentFrame.setReturnAddress(&parentFrame);

  AsyncStackFrame metadataMarker;
  markAsMetadataFrame(metadataMarker);

  AsyncStackFrame wrapperFrame;
  wrapperFrame.setReturnAddress(&wrapperFrame);
  linkInactiveFrames(wrapperFrame, metadataMarker, parentFrame);

  constexpr auto kCanary = std::numeric_limits<std::uintptr_t>::max();
  const auto checkWalk =
      [&](size_t maxAddresses,
          size_t expectedNumFrames,
          std::array<std::uintptr_t, 3> expected) {
        std::array addresses{kCanary, kCanary, kCanary};
        const auto numFrames = getAsyncStackTraceFromInitialFrame(
            &wrapperFrame, addresses.data(), maxAddresses);

        EXPECT_EQ(numFrames, expectedNumFrames);
        EXPECT_LT(numFrames, addresses.size());
        EXPECT_EQ(addresses, expected);
      };

  checkWalk(
      3,
      2,
      {reinterpret_cast<std::uintptr_t>(&wrapperFrame),
       reinterpret_cast<std::uintptr_t>(&parentFrame),
       kCanary});
  checkWalk(
      1,
      1,
      {reinterpret_cast<std::uintptr_t>(&wrapperFrame), kCanary, kCanary});
}

TEST(AsyncStack, InitialFrameWalkHandlesInitialMarker) {
  using namespace folly;
  using test::linkInactiveFrames;
  using test::markAsMetadataFrame;

  // Metadata scopes never start with a marker. Exercise the defensive case:
  // `[marker]` is empty; `[marker] -> parent` returns only `parent`.
  AsyncStackFrame parentFrame;
  parentFrame.setReturnAddress(&parentFrame);

  AsyncStackFrame metadataMarker;
  markAsMetadataFrame(metadataMarker);

  std::uintptr_t address = 1;
  EXPECT_EQ(
      getAsyncStackTraceFromInitialFrame(&metadataMarker, &address, 1), 0);
  EXPECT_EQ(address, 1);

  linkInactiveFrames(metadataMarker, parentFrame);
  EXPECT_EQ(
      getAsyncStackTraceFromInitialFrame(&metadataMarker, &address, 1), 1);
  EXPECT_EQ(address, reinterpret_cast<std::uintptr_t>(&parentFrame));
}

TEST(AsyncStack, InitialFrameWalkStopsOnAdjacentMetadataFrames) {
  using namespace folly;
  using test::linkInactiveFrames;
  using test::markAsMetadataFrame;

  // The API cannot create adjacent markers. The walker stops at any adjacent
  // pair because a marker cycle would neither fill the output nor terminate.
  AsyncStackFrame firstMarker;
  markAsMetadataFrame(firstMarker);
  AsyncStackFrame secondMarker;
  markAsMetadataFrame(secondMarker);
  AsyncStackFrame ordinaryFrame;
  ordinaryFrame.setReturnAddress(&ordinaryFrame);
  linkInactiveFrames(firstMarker, secondMarker, ordinaryFrame);

  std::uintptr_t address = 0;
  EXPECT_EQ(getAsyncStackTraceFromInitialFrame(&firstMarker, &address, 1), 0);
  EXPECT_EQ(address, 0);
}
#endif

TEST(AsyncStack, InitialFrameWalkHandlesEmptyAndZeroCapacity) {
  std::uintptr_t address = 1;
  EXPECT_EQ(folly::getAsyncStackTraceFromInitialFrame(nullptr, &address, 1), 0);
  EXPECT_EQ(address, 1);

  folly::AsyncStackFrame frame;
  frame.setReturnAddress(&frame);
  EXPECT_EQ(folly::getAsyncStackTraceFromInitialFrame(&frame, &address, 0), 0);
  EXPECT_EQ(address, 1);
}

TEST(AsyncStack, PushPop) {
  folly::detail::ScopedAsyncStackRoot scopedRoot{nullptr};

  auto& root = folly::getCurrentAsyncStackRoot();

  folly::AsyncStackFrame frame1;
  folly::AsyncStackFrame frame2;
  folly::AsyncStackFrame frame3;

  scopedRoot.activateFrame(frame1);

  CHECK_EQ(&frame1, root.getTopFrame());
  CHECK_EQ(&root, frame1.getStackRoot());

  folly::pushAsyncStackFrameCallerCallee(frame1, frame2);

  CHECK_EQ(&frame2, root.getTopFrame());
  CHECK_EQ(&frame1, frame2.getParentFrameUnsafe());
  CHECK_EQ(&root, frame2.getStackRoot());
  CHECK(frame1.getStackRoot() == nullptr);

  folly::pushAsyncStackFrameCallerCallee(frame2, frame3);

  CHECK_EQ(&frame3, root.getTopFrame());
  CHECK_EQ(&frame2, frame3.getParentFrameUnsafe());
  CHECK_EQ(&frame1, frame2.getParentFrameUnsafe());
  CHECK(frame1.getParentFrameUnsafe() == nullptr);
  CHECK(frame2.getStackRoot() == nullptr);

  folly::deactivateAsyncStackFrame(frame3);

  CHECK(root.getTopFrame() == nullptr);
  CHECK(frame3.getStackRoot() == nullptr);

  folly::activateAsyncStackFrame(root, frame3);

  CHECK_EQ(&frame3, root.getTopFrame());
  CHECK_EQ(&root, frame3.getStackRoot());

  folly::popAsyncStackFrameCallee(frame3);

  CHECK_EQ(&frame2, root.getTopFrame());
  CHECK_EQ(&root, frame2.getStackRoot());
  CHECK(frame3.getStackRoot() == nullptr);

  folly::popAsyncStackFrameCallee(frame2);

  CHECK_EQ(&frame1, root.getTopFrame());
  CHECK_EQ(&root, frame1.getStackRoot());
  CHECK(frame2.getStackRoot() == nullptr);

  folly::deactivateAsyncStackFrame(frame1);

  CHECK(root.getTopFrame() == nullptr);
  CHECK(frame1.getStackRoot() == nullptr);
}

static void checkNumFrames(size_t numFrames) {
  struct Aggregator {
    std::unordered_set<folly::AsyncStackFrame*> frames;
    void operator()(folly::AsyncStackFrame* frame) { frames.insert(frame); }
  } agg;
  folly::sweepSuspendedLeafFrames(agg);

  if constexpr (!folly::kIsDebug) {
    // tracking doesn't work in non-debug-builds
    CHECK_EQ(0, agg.frames.size());
  } else {
    CHECK_EQ(numFrames, agg.frames.size());
  }
}

TEST(AsyncStack, suspendedLeafTracking) {
  checkNumFrames(0);

  folly::AsyncStackFrame leafFrame;
  folly::activateSuspendedLeaf(leafFrame);
  CHECK(folly::isSuspendedLeafActive(leafFrame));
  checkNumFrames(1);

  folly::deactivateSuspendedLeaf(leafFrame);
  CHECK(!folly::isSuspendedLeafActive(leafFrame));
  checkNumFrames(0);
}
