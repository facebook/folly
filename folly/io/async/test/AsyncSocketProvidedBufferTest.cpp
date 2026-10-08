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

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/test/IoUringTestUtil.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::test;

namespace {

constexpr uint32_t kTinyRingBuffers = 4;

IoUringBackend::Options incrementalRingOptions() {
  auto options = ioUringOptionsWithProvidedBuffers();
  options.setEnableIncrementalBuffers(true);
  return options;
}

IoUringBackend::Options tinyRingOptions() {
  auto options = ioUringOptions();
  options.setInitialProvidedBuffers(2048, kTinyRingBuffers);
  return options;
}

IoUringBackend::Options tinyDynamicRingOptions() {
  auto options = tinyRingOptions();
  options.setProvidedBufferRingMode(
      IoUringOptions::ProvidedBufferRingMode::Dynamic);
  return options;
}

class AsyncSocketProvidedBufferTest : public IoUringTest {};

} // namespace

TEST_F(AsyncSocketProvidedBufferTest, ReceiveIntoStaticRing) {
  auto evb = makeIoUringEventBase(ioUringOptionsWithProvidedBuffers);
  const auto payload = makePayload(64 * 2048 + 1024);
  RetainingReadCallback rcb;

  receiveFromPeer(*evb, rcb, payload);

  EXPECT_EQ(payload, rcb.data());
  const auto stats = ioUringStats(*evb);
  EXPECT_GT(stats.cqe.providedBufferCount, 0);
  EXPECT_EQ(0, stats.cqe.bufMoreCount);
  EXPECT_EQ(0, stats.providedBuffer.enobufCount);
}

TEST_F(AsyncSocketProvidedBufferTest, IncrementalRingConsumesBuffersPartially) {
  auto evb = makeIoUringEventBase(incrementalRingOptions);
  const auto payload = makePayload(64 * 2048 + 1024);
  RetainingReadCallback rcb;

  receiveFromPeer(*evb, rcb, payload);

  EXPECT_EQ(payload, rcb.data());
  EXPECT_GT(ioUringStats(*evb).cqe.bufMoreCount, 0);
}

TEST_F(AsyncSocketProvidedBufferTest, ExhaustedRingFallsBackWithoutLosingData) {
  auto evb = makeIoUringEventBase(tinyRingOptions);
  const auto payload = makePayload(16 * 2048);
  RetainingReadCallback rcb;

  receiveFromPeer(*evb, rcb, payload);

  EXPECT_EQ(payload, rcb.data());
  const auto stats = ioUringStats(*evb);
  EXPECT_EQ(kTinyRingBuffers, stats.cqe.providedBufferCount);
  EXPECT_GT(stats.providedBuffer.enobufCount, 0);
}

TEST_F(AsyncSocketProvidedBufferTest, DynamicRingGrowsWhenBuffersAreHeld) {
  auto evb = makeIoUringEventBase(tinyDynamicRingOptions);
  auto* backend = dynamic_cast<IoUringBackend*>(evb->getBackend());
  const auto initialAreas = backend->bufferProvider()->areaCount();
  const auto payload = makePayload(16 * 2048);
  RetainingReadCallback rcb;

  receiveFromPeer(*evb, rcb, payload);

  EXPECT_EQ(payload, rcb.data());
  EXPECT_GT(ioUringStats(*evb).providedBuffer.areaCount, initialAreas);
}
