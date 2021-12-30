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

#include <folly/io/IOBuf.h>

#include <folly/portability/GTest.h>

std::atomic<size_t> gIOBufAlloc{};

void io_buf_alloc_cb(void* /*ptr*/, size_t size) noexcept {
  gIOBufAlloc += size;
}

void io_buf_free_cb(void* /*ptr*/, size_t size) noexcept {
  gIOBufAlloc -= size;
}

namespace {
struct IOBufTestInfo {
  std::unique_ptr<folly::IOBuf> ioBuf;
  size_t delta{0};

  void reset() { ioBuf.reset(); }
};
} // namespace

TEST(IOBufCB, AllocSizes) {
  std::vector<IOBufTestInfo> testInfoVec;
  size_t initialSize = gIOBufAlloc.load();
  size_t prevSize = initialSize;

  auto allocAndAppend = [&](size_t size) {
    auto buf = folly::IOBuf::create(size);
    size_t newSize = gIOBufAlloc.load();
    CHECK_GT(newSize, prevSize);
    size_t delta = newSize - prevSize;
    if (delta) {
      CHECK_GE(delta, size);
    }
    prevSize = newSize;

    IOBufTestInfo info;
    info.ioBuf = std::move(buf);
    info.delta = delta;

    testInfoVec.emplace_back(std::move(info));
  };
  // try with smaller allocations
  for (size_t i = 0; i < 1234; i++) {
    allocAndAppend(i);
  }

  // try with large allocations that will require an external buffer
  for (size_t i = 1; i < 100; i++) {
    allocAndAppend(i * 1024);
  }

  // deallocate
  for (size_t i = 0; i < testInfoVec.size(); i++) {
    testInfoVec[i].reset();
  }

  CHECK_EQ(initialSize, gIOBufAlloc.load());
}

TEST(IOBufCB, TakeOwnership) {
  size_t initialSize = gIOBufAlloc.load();
  static constexpr size_t kAllocSize = 1024;

  {
    auto buf = folly::IOBuf::takeOwnership(
        folly::IOBuf::SIZED_FREE, malloc(kAllocSize), kAllocSize, 0, 0);
    size_t newSize = gIOBufAlloc.load();
    CHECK_GE(newSize, initialSize + kAllocSize);
  }

  CHECK_EQ(initialSize, gIOBufAlloc.load());

  {
    folly::IOBuf buf(
        folly::IOBuf::TAKE_OWNERSHIP,
        folly::IOBuf::SIZED_FREE,
        malloc(kAllocSize),
        kAllocSize,
        0,
        0);
    size_t newSize = gIOBufAlloc.load();
    CHECK_GE(newSize, initialSize + kAllocSize);
  }

  CHECK_EQ(initialSize, gIOBufAlloc.load());
}

TEST(IOBufCB, MoveToFbString) {
  size_t initialSize = gIOBufAlloc.load();
  static constexpr size_t kAllocSize = 1024;

  LOG(INFO) << gIOBufAlloc.load();

  {
    auto buf = folly::IOBuf::takeOwnership(
        folly::IOBuf::SIZED_FREE, malloc(kAllocSize), kAllocSize, 0, 0);
    LOG(INFO) << gIOBufAlloc.load();
    size_t newSize = gIOBufAlloc.load();
    CHECK_GE(newSize, initialSize + kAllocSize);

    auto str = buf->moveToFbString();
    LOG(INFO) << gIOBufAlloc.load();
    CHECK_LT(gIOBufAlloc.load(), newSize);
  }
  LOG(INFO) << gIOBufAlloc.load();

  CHECK_EQ(initialSize, gIOBufAlloc.load());
}
